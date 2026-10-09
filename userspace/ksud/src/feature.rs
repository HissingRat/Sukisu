use anyhow::{Context, Result, bail};
use const_format::concatcp;
use std::collections::HashMap;
use std::fs::File;
use std::io::{Read, Write};
use std::os::fd::AsRawFd;
use std::path::Path;

use crate::defs;

const FEATURE_CONFIG_PATH: &str = concatcp!(defs::WORKING_DIR, ".feature_config");
#[allow(clippy::unreadable_literal)]
const FEATURE_MAGIC: u32 = 0x7f4b5355;
const FEATURE_VERSION: u32 = 1;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(u32)]
pub enum FeatureId {
    SuCompat = 0,
    KernelUmount = 1,
    // IDs 2 and 3 are reserved for upstream features.
    SelinuxHide = 4,
}

impl FeatureId {
    pub const fn from_u32(id: u32) -> Option<Self> {
        match id {
            0 => Some(Self::SuCompat),
            1 => Some(Self::KernelUmount),
            4 => Some(Self::SelinuxHide),
            _ => None,
        }
    }

    pub const fn name(self) -> &'static str {
        match self {
            Self::SuCompat => "su_compat",
            Self::KernelUmount => "kernel_umount",
            Self::SelinuxHide => "selinux_hide",
        }
    }

    pub const fn description(self) -> &'static str {
        match self {
            Self::SuCompat => {
                "SU Compatibility Mode - allows authorized apps to gain root via traditional 'su' command"
            }
            Self::KernelUmount => {
                "Kernel Umount - controls whether kernel automatically unmounts modules when not needed"
            }
            Self::SelinuxHide => "SELinux Hide - hide SELinux policy modifications from app UIDs",
        }
    }
}

fn query_feature(feature: FeatureId) -> std::io::Result<(u64, bool)> {
    match crate::ksucalls::get_feature(feature as u32) {
        Err(error)
            if feature == FeatureId::SelinuxHide
                && matches!(
                    error.raw_os_error(),
                    Some(libc::EINVAL | libc::ENOTTY | libc::EOPNOTSUPP)
                ) =>
        {
            Ok((0, false))
        }
        result => result,
    }
}

fn is_selinux_hide_pending(feature: FeatureId, value: u64, error: &std::io::Error) -> bool {
    feature == FeatureId::SelinuxHide && value == 1 && error.raw_os_error() == Some(libc::EAGAIN)
}

fn parse_feature_id(name: &str) -> Result<FeatureId> {
    match name {
        "su_compat" | "0" => Ok(FeatureId::SuCompat),
        "kernel_umount" | "1" => Ok(FeatureId::KernelUmount),
        "selinux_hide" | "4" => Ok(FeatureId::SelinuxHide),
        _ => bail!("Unknown feature: {name}"),
    }
}

// All read/modify/write paths share this lock; atomic replacement alone cannot
// prevent an unrelated concurrent feature save from overwriting a pending request.
fn lock_config() -> Result<File> {
    let directory = Path::new(defs::WORKING_DIR);
    crate::utils::ensure_dir_exists(directory)?;
    let lock = File::options()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(directory.join(".feature_config.lock"))
        .with_context(|| "Failed to open feature config lock")?;
    loop {
        // SAFETY: lock owns a live fd; flock does not retain a userspace pointer.
        if unsafe { libc::flock(lock.as_raw_fd(), libc::LOCK_EX) } == 0 {
            return Ok(lock);
        }
        let error = std::io::Error::last_os_error();
        if error.kind() != std::io::ErrorKind::Interrupted {
            return Err(error).with_context(|| "Failed to lock feature config");
        }
    }
}

pub fn load_binary_config() -> Result<HashMap<u32, u64>> {
    let path = Path::new(FEATURE_CONFIG_PATH);
    if !path.exists() {
        log::info!("Feature config not found, using defaults");
        return Ok(HashMap::new());
    }

    let mut file = File::open(path).with_context(|| "Failed to open feature config")?;

    let mut magic_buf = [0u8; 4];
    file.read_exact(&mut magic_buf)
        .with_context(|| "Failed to read magic")?;
    let magic = u32::from_le_bytes(magic_buf);

    if magic != FEATURE_MAGIC {
        bail!("Invalid feature config magic: expected 0x{FEATURE_MAGIC:08x}, got 0x{magic:08x}");
    }

    let mut version_buf = [0u8; 4];
    file.read_exact(&mut version_buf)
        .with_context(|| "Failed to read version")?;
    let version = u32::from_le_bytes(version_buf);

    if version != FEATURE_VERSION {
        log::warn!(
            "Feature config version mismatch: expected {FEATURE_VERSION}, got {version
            }",
        );
    }

    let mut count_buf = [0u8; 4];
    file.read_exact(&mut count_buf)
        .with_context(|| "Failed to read count")?;
    let count = u32::from_le_bytes(count_buf);

    let mut features = HashMap::new();
    for _ in 0..count {
        let mut id_buf = [0u8; 4];
        let mut value_buf = [0u8; 8];

        file.read_exact(&mut id_buf)
            .with_context(|| "Failed to read feature id")?;
        file.read_exact(&mut value_buf)
            .with_context(|| "Failed to read feature value")?;

        let id = u32::from_le_bytes(id_buf);
        let value = u64::from_le_bytes(value_buf);

        features.insert(id, value);
    }

    log::info!("Loaded {} features from config", features.len());
    Ok(features)
}

pub fn save_binary_config(features: &HashMap<u32, u64>) -> Result<()> {
    crate::utils::ensure_dir_exists(Path::new(defs::WORKING_DIR))?;

    let path = Path::new(FEATURE_CONFIG_PATH);
    // Replace atomically: a failed write must not truncate the previous boot configuration.
    let mut file = tempfile::NamedTempFile::new_in(Path::new(defs::WORKING_DIR))
        .with_context(|| "Failed to create temporary feature config")?;

    file.write_all(&FEATURE_MAGIC.to_le_bytes())
        .with_context(|| "Failed to write magic")?;

    file.write_all(&FEATURE_VERSION.to_le_bytes())
        .with_context(|| "Failed to write version")?;

    let count = features.len() as u32;
    file.write_all(&count.to_le_bytes())
        .with_context(|| "Failed to write count")?;

    for (&id, &value) in features {
        file.write_all(&id.to_le_bytes())
            .with_context(|| format!("Failed to write feature id {id}"))?;
        file.write_all(&value.to_le_bytes())
            .with_context(|| format!("Failed to write feature value for id {id}"))?;
    }

    file.as_file()
        .sync_all()
        .with_context(|| "Failed to sync feature config")?;

    file.persist(path)
        .with_context(|| "Failed to replace feature config")?;

    log::info!("Saved {} features to config", features.len());
    Ok(())
}

pub fn apply_config(features: &HashMap<u32, u64>) {
    log::info!("Applying feature configuration to kernel...");

    let mut applied = 0;
    for (&id, &value) in features {
        match crate::ksucalls::set_feature(id, value) {
            Ok(()) => {
                if let Some(feature_id) = FeatureId::from_u32(id) {
                    log::info!("Set feature {} to {value}", feature_id.name());
                } else {
                    log::info!("Set feature {id} to {value}");
                }
                applied += 1;
            }
            Err(e) => {
                log::warn!("Failed to set feature {id}: {e}");
            }
        }
    }

    log::info!("Applied {applied} features successfully");
}

pub fn get_feature(id: &str) -> Result<()> {
    let feature_id = parse_feature_id(id)?;
    let (value, supported) =
        query_feature(feature_id).with_context(|| format!("Failed to get feature {id}"))?;

    if !supported {
        println!("Feature '{id}' is not supported by kernel");
        return Ok(());
    }

    println!("Feature: {} ({})", feature_id.name(), feature_id as u32);
    println!("Description: {}", feature_id.description());
    println!("Value: {value}");
    println!(
        "Status: {}",
        if value != 0 { "enabled" } else { "disabled" }
    );

    if feature_id == FeatureId::SelinuxHide {
        let features = load_binary_config()?;
        if let Some(requested) = features.get(&(feature_id as u32))
            && *requested != value
        {
            println!("Requested value: {requested} (saved; reboot required)");
        }
    }

    Ok(())
}

pub fn get_feature_config(id: &str) -> Result<()> {
    let feature_id = parse_feature_id(id)?;

    let features = load_binary_config()?;
    let id_u32 = feature_id as u32;

    println!("Feature: {} ({})", feature_id.name(), id_u32);
    println!("Description: {}", feature_id.description());

    if let Some(value) = features.get(&id_u32) {
        println!("Value: {value}");
        println!(
            "Status: {}",
            if *value != 0 { "enabled" } else { "disabled" }
        );
    } else {
        println!("Not set in config");
    }

    Ok(())
}

pub fn set_feature(id: &str, value: u64, persist: bool) -> Result<()> {
    let feature_id = parse_feature_id(id)?;

    // Match the kernel/upstream nonzero boolean semantics while storing a canonical
    // request, so pending enables always persist as 1 rather than arbitrary values.
    let value = if feature_id == FeatureId::SelinuxHide {
        u64::from(value != 0)
    } else {
        value
    };

    // Check if this feature is managed by any module
    if let Ok(managed_features_map) = crate::module::get_managed_features() {
        // Find which modules manage this feature
        let managing_modules: Vec<&String> = managed_features_map
            .iter()
            .filter(|(_, features)| features.iter().any(|f| f == feature_id.name()))
            .map(|(module_id, _)| module_id)
            .collect();

        if !managing_modules.is_empty() {
            // Feature is managed, check if caller is an authorized module
            let caller_module = std::env::var("KSU_MODULE").unwrap_or_default();

            if caller_module.is_empty() || !managing_modules.contains(&&caller_module) {
                bail!(
                    "Feature '{}' is managed by module(s): {}. Direct modification is not allowed.",
                    feature_id.name(),
                    managing_modules
                        .iter()
                        .map(|s| s.as_str())
                        .collect::<Vec<_>>()
                        .join(", ")
                );
            }

            log::info!(
                "Module '{caller_module}' is setting managed feature '{}'",
                feature_id.name()
            );
        }
    }

    let _config_lock = if persist { Some(lock_config()?) } else { None };
    let pending_reboot = match crate::ksucalls::set_feature(feature_id as u32, value) {
        Ok(()) => false,
        Err(error) if persist && is_selinux_hide_pending(feature_id, value, &error) => true,
        Err(error) => {
            return Err(error).with_context(|| format!("Failed to set feature {id} to {value}"));
        }
    };

    if persist {
        let mut features = load_binary_config()?;
        features.insert(feature_id as u32, value);
        save_binary_config(&features)?;
    }

    if pending_reboot {
        println!(
            "Feature '{}' enable request saved; reboot to take full effect",
            feature_id.name()
        );
        return Ok(());
    }

    println!(
        "Feature '{}' set to {value} ({})",
        feature_id.name(),
        if value != 0 { "enabled" } else { "disabled" }
    );

    Ok(())
}

pub fn list_features() {
    println!("Available Features:");
    println!("{}", "=".repeat(80));

    // Get managed features from modules
    let managed_features_map = crate::module::get_managed_features().unwrap_or_default();

    // Build a reverse map: feature_name -> Vec<module_id>
    let mut feature_to_modules: HashMap<String, Vec<String>> = HashMap::new();
    for (module_id, feature_list) in &managed_features_map {
        for feature_name in feature_list {
            feature_to_modules
                .entry(feature_name.clone())
                .or_default()
                .push(module_id.clone());
        }
    }

    let all_features = [
        FeatureId::SuCompat,
        FeatureId::KernelUmount,
        FeatureId::SelinuxHide,
    ];

    for feature_id in &all_features {
        let id = *feature_id as u32;
        let status = match query_feature(*feature_id) {
            Ok((_, false)) => "NOT_SUPPORTED".to_string(),
            Ok((0, true)) => "DISABLED".to_string(),
            Ok((value, true)) => format!("ENABLED ({value})"),
            Err(error) => format!("ERROR ({error})"),
        };

        let managed_by = feature_to_modules.get(feature_id.name());
        let managed_mark = if managed_by.is_some() {
            " [MODULE_MANAGED]"
        } else {
            ""
        };

        println!(
            "[{}] {} (ID={}){}",
            status,
            feature_id.name(),
            id,
            managed_mark
        );
        println!("    {}", feature_id.description());

        if let Some(modules) = managed_by {
            println!(
                "    ⚠️  Managed by module(s): {} (forced to 0 on initialization)",
                modules.join(", ")
            );
        }

        println!();
    }
}

pub fn load_config_and_apply() -> Result<()> {
    let _config_lock = lock_config()?;
    let features = load_binary_config()?;

    if features.is_empty() {
        println!("No features found in config file");
        return Ok(());
    }

    apply_config(&features);
    println!("Feature configuration loaded and applied");
    Ok(())
}

pub fn save_config() -> Result<()> {
    let _config_lock = lock_config()?;
    let mut features = load_binary_config()?;

    let all_features = [
        FeatureId::SuCompat,
        FeatureId::KernelUmount,
        FeatureId::SelinuxHide,
    ];

    for feature_id in &all_features {
        let id = *feature_id as u32;
        if let Ok((value, supported)) = query_feature(*feature_id)
            && supported
        {
            // A pending SELinux enable remains disabled in the kernel until reboot.
            // Other feature switches call `feature save`; retain its requested boot value.
            let pending_enable = feature_id == &FeatureId::SelinuxHide
                && value == 0
                && features.get(&id) == Some(&1);
            if !pending_enable {
                features.insert(id, value);
            }
            log::info!("Saved feature {} = {}", feature_id.name(), features[&id]);
        }
    }

    save_binary_config(&features)?;
    println!(
        "Current feature states saved to config file ({} features)",
        features.len()
    );
    Ok(())
}

pub fn check_feature(id: &str) -> Result<()> {
    let feature_id = parse_feature_id(id)?;

    // Check if this feature is managed by any module
    let managed_features_map = crate::module::get_managed_features().unwrap_or_default();
    let is_managed = managed_features_map
        .values()
        .any(|features| features.iter().any(|f| f == feature_id.name()));

    if is_managed {
        println!("managed");
        return Ok(());
    }

    // Check if the feature is supported by kernel
    let (_value, supported) =
        query_feature(feature_id).with_context(|| format!("Failed to get feature {id}"))?;

    if supported {
        println!("supported");
    } else {
        println!("unsupported");
    }

    Ok(())
}

pub fn init_features() -> Result<()> {
    let _config_lock = lock_config()?;
    log::info!("Initializing features from config...");

    let mut features = load_binary_config()?;

    // Get managed features from active modules and skip them during init
    if let Ok(managed_features_map) = crate::module::get_managed_features() {
        if !managed_features_map.is_empty() {
            log::info!(
                "Found {} modules managing features",
                managed_features_map.len()
            );

            // Build a set of all managed feature IDs to skip
            for (module_id, feature_list) in &managed_features_map {
                log::info!(
                    "Module '{module_id}' manages {} feature(s)",
                    feature_list.len()
                );
                for feature_name in feature_list {
                    if let Ok(feature_id) = parse_feature_id(feature_name) {
                        let feature_id_u32 = feature_id as u32;
                        // Remove managed features from config, let modules control them
                        if features.remove(&feature_id_u32).is_some() {
                            log::info!(
                                "  - Skipping managed feature '{feature_name}' (controlled by module: {module_id})",
                            );
                        } else {
                            log::info!(
                                "  - Feature '{feature_name}' is managed by module '{module_id}', skipping",
                            );
                        }
                    } else {
                        log::warn!(
                            "  - Unknown managed feature '{feature_name}' from module '{module_id}', ignoring",
                        );
                    }
                }
            }
        }
    } else {
        log::warn!(
            "Failed to get managed features from modules, continuing with normal initialization"
        );
    }

    if features.is_empty() {
        log::info!("No features to apply, skipping initialization");
        return Ok(());
    }

    apply_config(&features);

    // Save the configuration (excluding managed features)
    save_binary_config(&features)?;
    log::info!("Saved feature configuration to file");

    Ok(())
}

// Run on the host: exercises the production feature/config implementation with mock ioctls.
#![cfg(not(target_os = "android"))]
#![allow(dead_code)]

use std::collections::HashMap;
use std::sync::Mutex;

mod defs {
    pub const WORKING_DIR: &str = "/tmp/ksud-selinux-hide-regression/";
}
mod utils {
    pub fn ensure_dir_exists(path: &std::path::Path) -> anyhow::Result<()> {
        std::fs::create_dir_all(path)?;
        Ok(())
    }
}
mod module {
    pub fn get_managed_features() -> anyhow::Result<HashMap<String, Vec<String>>> {
        if super::KERNEL.lock().unwrap().as_ref().unwrap().managed {
            Ok(HashMap::from([(
                format!("regression_module_{}", std::process::id()),
                vec!["selinux_hide".to_owned()],
            )]))
        } else {
            Ok(HashMap::new())
        }
    }
    use super::HashMap;
}
#[derive(Default)]
struct Kernel {
    values: HashMap<u32, u64>,
    set_error: Option<i32>,
    get_error: Option<i32>,
    unsupported: bool,
    managed: bool,
    set_calls: Vec<(u32, u64)>,
    requested_hide: bool,
}
static KERNEL: Mutex<Option<Kernel>> = Mutex::new(None);
mod ksucalls {
    use super::KERNEL;
    pub fn get_feature(id: u32) -> std::io::Result<(u64, bool)> {
        let guard = KERNEL.lock().unwrap();
        let kernel = guard.as_ref().unwrap();
        if let Some(error) = kernel.get_error {
            return Err(std::io::Error::from_raw_os_error(error));
        }
        Ok((*kernel.values.get(&id).unwrap_or(&0), !kernel.unsupported))
    }
    pub fn set_feature(id: u32, value: u64) -> std::io::Result<()> {
        let mut guard = KERNEL.lock().unwrap();
        let kernel = guard.as_mut().unwrap();
        kernel.set_calls.push((id, value));
        if id == 4 {
            kernel.requested_hide = value != 0;
        }
        if let Some(error) = kernel.set_error {
            return Err(std::io::Error::from_raw_os_error(error));
        }
        kernel.values.insert(id, value);
        Ok(())
    }
}
#[path = "../../../src/feature.rs"]
mod feature;

#[test]
fn pending_requests_survive_save_and_cancel_without_masking_errors() {
    let dir = std::path::Path::new(defs::WORKING_DIR);
    std::fs::create_dir_all(dir).unwrap();
    let config_path = dir.join(".feature_config");
    let _ = std::fs::remove_file(&config_path);
    *KERNEL.lock().unwrap() = Some(Kernel::default());

    assert_eq!(
        feature::FeatureId::from_u32(4),
        Some(feature::FeatureId::SelinuxHide)
    );
    assert_eq!(feature::FeatureId::from_u32(2), None);
    assert_eq!(feature::FeatureId::from_u32(3), None);
    assert_eq!(feature::FeatureId::SelinuxHide.name(), "selinux_hide");

    // Missing backup leaves actual state disabled. Only --persist accepts EAGAIN.
    KERNEL.lock().unwrap().as_mut().unwrap().set_error = Some(libc::EAGAIN);
    assert!(feature::set_feature("selinux_hide", 1, false).is_err());
    assert!(feature::load_binary_config().unwrap().is_empty());
    feature::set_feature("4", 1, true).unwrap();
    assert_eq!(ksucalls::get_feature(4).unwrap().0, 0);
    assert_eq!(feature::load_binary_config().unwrap().get(&4), Some(&1));

    // An unrelated feature save must not replace pending 1 with the actual 0.
    KERNEL.lock().unwrap().as_mut().unwrap().set_error = None;
    feature::set_feature("kernel_umount", 1, false).unwrap();
    feature::save_config().unwrap();
    let saved = feature::load_binary_config().unwrap();
    assert_eq!(saved.get(&4), Some(&1));
    assert_eq!(saved.get(&1), Some(&1));

    // A config writer must wait while another process owns the transaction lock.
    use std::os::fd::AsRawFd;
    let transaction = std::fs::File::open(dir.join(".feature_config.lock")).unwrap();
    assert_eq!(
        unsafe { libc::flock(transaction.as_raw_fd(), libc::LOCK_EX) },
        0
    );
    let (started_tx, started_rx) = std::sync::mpsc::channel();
    let (done_tx, done_rx) = std::sync::mpsc::channel();
    let writer = std::thread::spawn(move || {
        started_tx.send(()).unwrap();
        feature::save_config().unwrap();
        done_tx.send(()).unwrap();
    });
    started_rx.recv().unwrap();
    assert!(matches!(
        done_rx.recv_timeout(std::time::Duration::from_millis(50)),
        Err(std::sync::mpsc::RecvTimeoutError::Timeout)
    ));
    drop(transaction);
    done_rx
        .recv_timeout(std::time::Duration::from_secs(2))
        .unwrap();
    writer.join().unwrap();

    // Different UI switches can save simultaneously with the pending request.
    let mut before_pending = feature::load_binary_config().unwrap();
    before_pending.insert(4, 0);
    feature::save_binary_config(&before_pending).unwrap();
    KERNEL.lock().unwrap().as_mut().unwrap().set_error = Some(libc::EAGAIN);
    std::thread::scope(|scope| {
        scope.spawn(|| {
            for _ in 0..32 {
                feature::save_config().unwrap();
            }
        });
        scope.spawn(|| {
            for _ in 0..32 {
                feature::set_feature("selinux_hide", 1, true).unwrap();
            }
        });
    });
    assert_eq!(feature::load_binary_config().unwrap().get(&4), Some(&1));
    KERNEL.lock().unwrap().as_mut().unwrap().set_error = None;

    // Cancel before reboot explicitly writes 0; it survives subsequent saves.
    feature::set_feature("selinux_hide", 0, true).unwrap();
    feature::save_config().unwrap();
    assert_eq!(feature::load_binary_config().unwrap().get(&4), Some(&0));
    assert!(!KERNEL.lock().unwrap().as_ref().unwrap().requested_hide);

    // Other ioctl errors cannot persist a request or claim success.
    KERNEL.lock().unwrap().as_mut().unwrap().set_error = Some(libc::EIO);
    assert!(feature::set_feature("selinux_hide", 1, true).is_err());
    assert_eq!(feature::load_binary_config().unwrap().get(&4), Some(&0));
    assert!(KERNEL.lock().unwrap().as_ref().unwrap().requested_hide);
    assert_eq!(ksucalls::get_feature(4).unwrap().0, 0);
    // Explicit clear must still issue the ioctl when the core getter is already 0.
    KERNEL.lock().unwrap().as_mut().unwrap().set_error = None;
    feature::set_feature("selinux_hide", 0, true).unwrap();
    assert!(!KERNEL.lock().unwrap().as_ref().unwrap().requested_hide);
    assert_eq!(feature::load_binary_config().unwrap().get(&4), Some(&0));
    KERNEL.lock().unwrap().as_mut().unwrap().set_error = Some(libc::EAGAIN);
    assert!(feature::set_feature("selinux_hide", 0, true).is_err());
    // Pinned upstream accepts any nonzero as enabled. Persist the canonical 1.
    feature::set_feature("selinux_hide", 2, true).unwrap();
    assert_eq!(feature::load_binary_config().unwrap().get(&4), Some(&1));
    assert_eq!(ksucalls::get_feature(4).unwrap().0, 0); // Still EAGAIN, not falsely active.
    KERNEL.lock().unwrap().as_mut().unwrap().set_error = None;
    for value in [2, 7, u64::MAX] {
        feature::set_feature("selinux_hide", value, true).unwrap();
        assert_eq!(ksucalls::get_feature(4).unwrap().0, 1);
        assert_eq!(feature::load_binary_config().unwrap().get(&4), Some(&1));
    }
    feature::set_feature("kernel_umount", 7, false).unwrap();
    assert!(
        KERNEL
            .lock()
            .unwrap()
            .as_ref()
            .unwrap()
            .set_calls
            .contains(&(1, 7))
    );

    // Old unknown-ID errors become unsupported; permission/IO failures stay errors.
    KERNEL.lock().unwrap().as_mut().unwrap().get_error = Some(libc::EINVAL);
    assert!(feature::check_feature("selinux_hide").is_ok());
    KERNEL.lock().unwrap().as_mut().unwrap().get_error = Some(libc::EIO);
    assert!(feature::check_feature("selinux_hide").is_err());

    // Unknown feature IDs survive saves, so a newer kernel request is not erased.
    *KERNEL.lock().unwrap() = Some(Kernel::default());
    let mut with_future = feature::load_binary_config().unwrap();
    with_future.insert(88, 7);
    feature::save_binary_config(&with_future).unwrap();
    feature::save_config().unwrap();
    assert_eq!(feature::load_binary_config().unwrap().get(&88), Some(&7));

    // Corrupt config makes the operation fail without destroying prior bytes.
    // The ioctl may already have succeeded: expose that actual state, never claim rollback.
    let good_config = std::fs::read(&config_path).unwrap();
    let invalid_config = b"intentionally corrupt feature config";
    std::fs::write(&config_path, invalid_config).unwrap();
    assert!(feature::set_feature("selinux_hide", 1, true).is_err());
    assert_eq!(ksucalls::get_feature(4).unwrap().0, 1);
    assert_eq!(std::fs::read(&config_path).unwrap(), invalid_config);
    std::fs::write(&config_path, &good_config).unwrap();

    // Atomic replacement cannot replace a directory; failed writes leave it intact
    // and do not leave a temporary feature config as a partially written final file.
    std::fs::remove_file(&config_path).unwrap();
    std::fs::create_dir(&config_path).unwrap();
    assert!(feature::save_binary_config(&HashMap::from([(4, 1)])).is_err());
    assert!(config_path.is_dir());
    assert!(std::fs::read_dir(dir).unwrap().all(|entry| {
        !entry
            .unwrap()
            .file_name()
            .to_string_lossy()
            .starts_with(".tmp")
    }));
    std::fs::remove_dir(&config_path).unwrap();
    std::fs::write(&config_path, good_config).unwrap();

    // A module-owned feature is rejected before ioctl and skipped during boot init.
    let mut owned = feature::load_binary_config().unwrap();
    owned.insert(4, 1);
    feature::save_binary_config(&owned).unwrap();
    {
        let mut guard = KERNEL.lock().unwrap();
        let kernel = guard.as_mut().unwrap();
        kernel.managed = true;
        kernel.values.insert(4, 0);
        kernel.set_calls.clear();
    }
    assert!(feature::set_feature("selinux_hide", 1, true).is_err());
    assert!(
        KERNEL
            .lock()
            .unwrap()
            .as_ref()
            .unwrap()
            .set_calls
            .is_empty()
    );
    feature::init_features().unwrap();
    assert_eq!(ksucalls::get_feature(4).unwrap().0, 0);
    assert!(!feature::load_binary_config().unwrap().contains_key(&4));
    KERNEL.lock().unwrap().as_mut().unwrap().managed = false;

    // Explicit supported=false and all old unknown-ID errno variants are graceful.
    KERNEL.lock().unwrap().as_mut().unwrap().unsupported = true;
    assert!(feature::check_feature("selinux_hide").is_ok());
    for error in [libc::EINVAL, libc::ENOTTY, libc::EOPNOTSUPP] {
        KERNEL.lock().unwrap().as_mut().unwrap().get_error = Some(error);
        assert!(feature::check_feature("selinux_hide").is_ok());
    }
    for error in [libc::EIO, libc::EPERM, libc::EBADF] {
        KERNEL.lock().unwrap().as_mut().unwrap().get_error = Some(error);
        assert!(feature::check_feature("selinux_hide").is_err());
    }

    // Reboot applies the saved enable to actual state when a backup is available.
    let kernel = Kernel::default();
    *KERNEL.lock().unwrap() = Some(kernel);
    let mut next_boot = feature::load_binary_config().unwrap();
    next_boot.insert(4, 1);
    feature::save_binary_config(&next_boot).unwrap();
    feature::load_config_and_apply().unwrap();
    assert_eq!(ksucalls::get_feature(4).unwrap().0, 1);
    std::fs::remove_dir_all(dir).unwrap();
}

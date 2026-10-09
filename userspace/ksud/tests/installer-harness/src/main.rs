// Compile the real patcher and embedded resources, not a copied implementation.
#[path = "../../../src/assets.rs"]
mod assets;
#[path = "../../../src/boot_patch.rs"]
mod boot_patch;

use clap::Parser;

#[derive(Parser)]
struct Args {
    #[command(flatten)]
    patch: boot_patch::BootPatchArgs,
}

fn main() -> anyhow::Result<()> {
    assert_eq!(assets::list_supported_kmi(), ["android13-5.15"]);
    boot_patch::patch(Args::parse().patch)
}

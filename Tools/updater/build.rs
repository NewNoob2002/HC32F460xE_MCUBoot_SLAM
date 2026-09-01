use std::collections::HashMap;
use std::env;
use std::fs;
use std::path::PathBuf;

const IDENTITY_KEYS: &[&str] = &[
    "HC32_PRODUCT_CLASS",
    "HC32_HARDWARE_ID",
    "HC32_BOARD_ID",
    "HC32_BOARD_REVISION",
    "HC32_USB_VID",
    "HC32_USB_BOOT_PID",
    "HC32_USB_APPLICATION_PID",
    "HC32_USB_APPLICATION_PID_MIN",
    "HC32_USB_APPLICATION_PID_MAX",
    "HC32_USB_MANUFACTURER",
    "HC32_USB_BOOT_PRODUCT",
    "HC32_USB_APPLICATION_PRODUCT",
    "HC32_USB_SERIAL_PREFIX",
];

fn main() {
    generate_product_identity();
    slint_build::compile("ui/app.slint").expect("failed to compile updater UI");
}

fn generate_product_identity() {
    println!("cargo:rerun-if-env-changed=HC32_PRODUCT_IDENTITY_FILE");
    let manifest_dir = PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").unwrap());
    let path = env::var_os("HC32_PRODUCT_IDENTITY_FILE")
        .map(PathBuf::from)
        .unwrap_or_else(|| manifest_dir.join("../../Config/Product/ProductIdentity.env"));
    println!("cargo:rerun-if-changed={}", path.display());

    let text = fs::read_to_string(&path).unwrap_or_else(|error| {
        panic!(
            "failed to read product identity {}: {error}",
            path.display()
        )
    });
    let mut values = HashMap::new();
    for line in text.lines().map(str::trim) {
        if line.is_empty() || line.starts_with('#') {
            continue;
        }
        let (name, value) = line
            .split_once('=')
            .unwrap_or_else(|| panic!("invalid product identity line: {line}"));
        assert!(
            IDENTITY_KEYS.contains(&name),
            "unknown product identity key: {name}"
        );
        assert!(
            values.insert(name, value).is_none(),
            "duplicate product identity key: {name}"
        );
    }

    let class = required(&values, "HC32_PRODUCT_CLASS");
    let hardware_id = number(required(&values, "HC32_HARDWARE_ID"), u32::MAX as u64);
    let board_id = number(required(&values, "HC32_BOARD_ID"), u32::MAX as u64);
    let board_revision = number(required(&values, "HC32_BOARD_REVISION"), u16::MAX as u64);
    let usb_vid = number(required(&values, "HC32_USB_VID"), u16::MAX as u64);
    let usb_boot_pid = number(required(&values, "HC32_USB_BOOT_PID"), u16::MAX as u64);
    let usb_application_pid = number(
        required(&values, "HC32_USB_APPLICATION_PID"),
        u16::MAX as u64,
    );
    let usb_application_pid_min = number(
        required(&values, "HC32_USB_APPLICATION_PID_MIN"),
        u16::MAX as u64,
    );
    let usb_application_pid_max = number(
        required(&values, "HC32_USB_APPLICATION_PID_MAX"),
        u16::MAX as u64,
    );
    let manufacturer = required(&values, "HC32_USB_MANUFACTURER");
    let boot_product = required(&values, "HC32_USB_BOOT_PRODUCT");
    let application_product = required(&values, "HC32_USB_APPLICATION_PRODUCT");
    let serial_prefix = required(&values, "HC32_USB_SERIAL_PREFIX");
    assert!(
        usb_vid != 0
            && usb_boot_pid != 0
            && usb_application_pid != 0
            && usb_application_pid_min != 0,
        "USB VID and PIDs must be non-zero"
    );
    assert!(
        usb_application_pid_min <= usb_application_pid
            && usb_application_pid <= usb_application_pid_max
            && !(usb_application_pid_min..=usb_application_pid_max).contains(&usb_boot_pid),
        "Application PID and approved range must exclude the Boot PID"
    );

    if env::var("PROFILE").as_deref() == Ok("release") && class != "production" {
        panic!("release requires HC32_PRODUCT_CLASS=production");
    }

    let generated = format!(
        "pub const HARDWARE_ID: u32 = {hardware_id};\n\
         pub const BOARD_ID: u32 = {board_id};\n\
         pub const BOARD_REVISION: u16 = {board_revision};\n\
         pub const USB_VID: u16 = {usb_vid};\n\
         pub const USB_BOOT_PID: u16 = {usb_boot_pid};\n\
         pub const USB_APPLICATION_PID: u16 = {usb_application_pid};\n\
         pub const USB_APPLICATION_PID_MIN: u16 = {usb_application_pid_min};\n\
         pub const USB_APPLICATION_PID_MAX: u16 = {usb_application_pid_max};\n\
         pub const USB_MANUFACTURER: &str = {manufacturer:?};\n\
         pub const USB_BOOT_PRODUCT: &str = {boot_product:?};\n\
         pub const USB_APPLICATION_PRODUCT: &str = {application_product:?};\n\
         pub const USB_SERIAL_PREFIX: &str = {serial_prefix:?};\n"
    );
    fs::write(
        PathBuf::from(env::var_os("OUT_DIR").unwrap()).join("product_identity.rs"),
        generated,
    )
    .expect("write generated product identity");
}

fn required<'a>(values: &'a HashMap<&str, &str>, name: &str) -> &'a str {
    values
        .get(name)
        .copied()
        .filter(|value| !value.is_empty())
        .unwrap_or_else(|| panic!("missing {name} in product identity"))
}

fn number(value: &str, maximum: u64) -> u64 {
    let parsed = value
        .strip_prefix("0x")
        .or_else(|| value.strip_prefix("0X"))
        .map(|hex| u64::from_str_radix(hex, 16))
        .unwrap_or_else(|| value.parse())
        .unwrap_or_else(|_| panic!("invalid product identity number: {value}"));
    assert!(
        parsed <= maximum,
        "product identity number is too large: {value}"
    );
    parsed
}

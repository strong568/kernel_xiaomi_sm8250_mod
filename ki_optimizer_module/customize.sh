#!/system/bin/sh
# Ki-Optimizer Installation Script (KernelSU / Magisk)

SKIPUNZIP=0

KERNEL_VER="$(uname -r)"

ui_print "  Ki-Kernel Settings v1.3 (Rust Daemon v1.1)"

# Strict Ki-Kernel Verification
is_ki_kernel=0
if echo "$KERNEL_VER" | grep -qi "ki-kernel"; then
    is_ki_kernel=1
elif [ -f /proc/ki_profile ] || [ -d /sys/kernel/ki_profile ]; then
    is_ki_kernel=1
fi

if [ "$is_ki_kernel" -eq 0 ]; then
    ui_print " "
    ui_print "****************************************************"
    ui_print "*   ❌ INSTALLATION ABORTED: INCOMPATIBLE KERNEL   *"
    ui_print "****************************************************"
    ui_print "! Current Kernel: $KERNEL_VER"
    ui_print "! This module STRICTLY REQUIRES Ki-Kernel."
    ui_print "! Third-party & stock kernels are NOT supported."
    ui_print "! Please flash Ki-Kernel first, then try again."
    ui_print "****************************************************"
    ui_print " "
    abort "! Installation aborted: Ki-Kernel is required."
fi

ui_print "- Ki-Kernel detected! [OK]"
ui_print "- Kernel Version: $KERNEL_VER"

# Setup configuration file in /sdcard
ui_print "- Setting up configuration file..."
if [ ! -f /sdcard/ki_optimizer.conf ]; then
    cp -f "$MODPATH/ki_optimizer.conf" /sdcard/ki_optimizer.conf 2>/dev/null
    chmod 666 /sdcard/ki_optimizer.conf 2>/dev/null
    ui_print "- Created config file: /sdcard/ki_optimizer.conf"
else
    ui_print "- Existing config found: /sdcard/ki_optimizer.conf (preserved)"
fi


# Set executable permissions
set_perm_recursive "$MODPATH" 0 0 0755 0644
set_perm "$MODPATH/service.sh" 0 0 0755
set_perm "$MODPATH/bin/ki" 0 0 0755
set_perm "$MODPATH/bin/ki_daemon" 0 0 0755
set_perm_recursive "$MODPATH/webroot" 0 0 0755 0644

# Link ki CLI and ki_daemon directly to root PATH without touching /system
# (Prevents OverlayFS conflicts with KernelSU metamodule & Zygisk Next)
for bin_dir in /data/adb/ksu/bin /data/adb/ap/bin /data/adb/magisk; do
    if [ -d "$bin_dir" ]; then
        ln -sf "$MODPATH/bin/ki" "$bin_dir/ki" 2>/dev/null
        ln -sf "$MODPATH/bin/ki_daemon" "$bin_dir/ki_daemon" 2>/dev/null
    fi
done

ui_print " "
ui_print "===================================================="
ui_print "✔ Ki-Optimizer Installed Successfully!"
ui_print "===================================================="

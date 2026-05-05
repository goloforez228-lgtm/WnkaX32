#!/bin/bash
set -e

ISO_NAME="wnka32.iso"
IMG_NAME="wnka32.img"
KERNEL_BIN="iso_root/boot/kernel.bin"
LOG_FILE="build_$(date +%Y%m%d_%H%M%S).log"
START_TIME=$(date +%s)

GREEN='\033[0;32m'
RED='\033[0;31m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
PURPLE='\033[0;35m'
CYAN='\033[0;36m'
NC='\033[0m'

OBJECTS=""

log() { echo "[$(date +%H:%M:%S)] $1" | tee -a "$LOG_FILE"; }
status() { echo -e "${GREEN}✅ $1${NC}"; log "SUCCESS: $1"; }
error() { echo -e "${RED}❌ ERROR: $1${NC}" >&2; log "ERROR: $1"; exit 1; }
warning() { echo -e "${YELLOW}⚠️  $1${NC}"; log "WARNING: $1"; }
info() { echo -e "${BLUE}ℹ️  $1${NC}"; log "INFO: $1"; }
success() { echo -e "${PURPLE}✨ $1${NC}"; log "SUCCESS: $1"; }

check_deps() {
    log "Checking dependencies..."
    local deps=("g++" "nasm" "ld" "grub-mkrescue" "qemu-system-i386" "xorriso" "dd" "mkfs.vfat" "parted")
    local missing=()
    for dep in "${deps[@]}"; do
        if ! command -v "$dep" &>/dev/null; then missing+=("$dep"); fi
    done
    
    if [ ${#missing[@]} -gt 0 ]; then
        warning "Missing: ${missing[*]}"
        info "Install missing packages:"
        info "  sudo apt install grub-pc-bin xorriso mtools parted dosfstools"
    else
        status "All dependencies found"
    fi
}

clean() {
    log "Cleaning..."
    rm -rf iso_root *.o *.iso *.img build/
    find . -name "*.o" -type f -delete
    find . -name "*.a" -type f -delete
    find . -name "*.so" -type f -delete
    status "Clean completed"
}

# ===== COMPILATION =====
compile_boot() {
    log "Compiling bootloader..."
    nasm -f elf32 boot/boot.asm -o boot/boot.o
    OBJECTS="$OBJECTS boot/boot.o"
    status "Bootloader compiled"
}

compile_kernel() {
    log "Compiling kernel..."
    g++ -m32 -ffreestanding -O2 -nostdlib -fno-stack-protector \
        -fno-exceptions -fno-rtti -std=c++11 \
        -I. -Iinclude -Iboot -Idrivers -Ifs -Ithemes -Isys \
        -c boot/kernel.cpp -o boot/kernel.o
    OBJECTS="$OBJECTS boot/kernel.o"
    status "Kernel compiled"
}

compile_shell() {
    log "Compiling shell..."
    if [ -f "sys/shell.cpp" ]; then
        g++ -m32 -ffreestanding -O2 -nostdlib -fno-stack-protector \
            -fno-exceptions -fno-rtti -std=c++11 \
            -I. -Iinclude -Iboot -Idrivers -Ifs -Ithemes -Isys \
            -c sys/shell.cpp -o sys/shell.o
        OBJECTS="$OBJECTS sys/shell.o"
        status "Shell compiled"
    else
        warning "sys/shell.cpp not found"
    fi
}

compile_drivers() {
    log "Compiling drivers..."
    local driver_count=0
    for driver in ata ahci mouse; do
        if [ -f "drivers/${driver}.cpp" ]; then
            g++ -m32 -ffreestanding -O2 -nostdlib -fno-stack-protector \
                -fno-exceptions -fno-rtti -std=c++11 \
                -I. -Iinclude -Idrivers \
                -c "drivers/${driver}.cpp" -o "drivers/${driver}.o"
            OBJECTS="$OBJECTS drivers/${driver}.o"
            driver_count=$((driver_count + 1))
        fi
    done
    status "Drivers compiled: $driver_count"
}

compile_fs() {
    log "Compiling filesystem (wnkfs.cpp)..."
    if [ -f "wnkfs.cpp" ]; then
        g++ -m32 -ffreestanding -O2 -nostdlib -fno-stack-protector \
            -fno-exceptions -fno-rtti -std=c++11 \
            -I. -Iinclude -Idrivers -Ifs -Ithemes \
            -c wnkfs.cpp -o wnkfs.o
        OBJECTS="$OBJECTS wnkfs.o"
        local size=$(stat -c%s wnkfs.cpp 2>/dev/null || stat -f%z wnkfs.cpp 2>/dev/null)
        status "WNKFS compiled (size: $size bytes)"
    else
        error "wnkfs.cpp not found in root directory!"
    fi
}

compile_resource_monitor() {
    log "Compiling resource monitor..."
    
    if [ -f "themes/resource_monitor.cpp" ]; then
        g++ -m32 -ffreestanding -O2 -nostdlib -fno-stack-protector \
            -fno-exceptions -fno-rtti -std=c++11 \
            -I. -Iinclude -Ithemes \
            -c themes/resource_monitor.cpp -o themes/resource_monitor.o
        OBJECTS="$OBJECTS themes/resource_monitor.o"
        status "Resource monitor compiled from themes/"
        
    elif [ -f "utils/resource_monitor.cpp" ]; then
        g++ -m32 -ffreestanding -O2 -nostdlib -fno-stack-protector \
            -fno-exceptions -fno-rtti -std=c++11 \
            -I. -Iinclude \
            -c utils/resource_monitor.cpp -o utils/resource_monitor.o
        OBJECTS="$OBJECTS utils/resource_monitor.o"
        status "Resource monitor compiled from utils/"
        
    elif [ -f "resource_monitor.cpp" ]; then
        g++ -m32 -ffreestanding -O2 -nostdlib -fno-stack-protector \
            -fno-exceptions -fno-rtti -std=c++11 \
            -I. -Iinclude \
            -c resource_monitor.cpp -o resource_monitor.o
        OBJECTS="$OBJECTS resource_monitor.o"
        status "Resource monitor compiled from root"
        
    else
        warning "resource_monitor.cpp not found - using built-in wnkfs functions"
    fi
}

compile_themes() {
    log "Compiling themes..."
    local theme_count=0
    
    if [ -f "themes/themes.cpp" ]; then
        g++ -m32 -ffreestanding -O2 -nostdlib -fno-stack-protector \
            -fno-exceptions -fno-rtti -std=c++11 \
            -I. -Iinclude -Ithemes \
            -c themes/themes.cpp -o themes/themes.o
        OBJECTS="$OBJECTS themes/themes.o"
        theme_count=$((theme_count + 1))
    fi
    
    if [ -f "themes/screensaver.cpp" ]; then
        g++ -m32 -ffreestanding -O2 -nostdlib -fno-stack-protector \
            -fno-exceptions -fno-rtti -std=c++11 \
            -I. -Iinclude -Ithemes \
            -c themes/screensaver.cpp -o themes/screensaver.o
        OBJECTS="$OBJECTS themes/screensaver.o"
        theme_count=$((theme_count + 1))
    fi
    
    status "Themes compiled: $theme_count"
}

link_kernel() {
    log "Linking kernel..."
    mkdir -p iso_root/boot/grub
    
    echo "📦 Object files to link:"
    local missing_objs=0
    local obj_list=""
    
    for obj in $OBJECTS; do
        if [ -f "$obj" ]; then
            local size=$(stat -c%s "$obj" 2>/dev/null || stat -f%z "$obj" 2>/dev/null)
            echo "  ✅ $obj ($size bytes)"
            obj_list="$obj_list $obj"
        else
            echo "  ❌ $obj (MISSING!)"
            missing_objs=1
        fi
    done
    
    if [ $missing_objs -eq 1 ]; then
        echo "🔍 Searching for all .o files:"
        find . -name "*.o" -type f | while read -r found; do
            echo "  Found: $found"
        done
        error "Some object files are missing. Check paths above!"
    fi
    
    # Проверяем linker script
    if [ ! -f "linker.ld" ]; then
        echo "⚠️  linker.ld not found! Creating default..."
        cat > linker.ld << 'LINKER'
ENTRY(_start)

SECTIONS
{
    . = 1M;

    .text BLOCK(4K) : ALIGN(4K)
    {
        *(.multiboot)
        *(.text)
    }

    .rodata BLOCK(4K) : ALIGN(4K)
    {
        *(.rodata)
    }

    .data BLOCK(4K) : ALIGN(4K)
    {
        *(.data)
    }

    .bss BLOCK(4K) : ALIGN(4K)
    {
        *(COMMON)
        *(.bss)
    }
}
LINKER
        echo "✅ Created linker.ld"
    fi
    
    # Линкуем
    echo "🔗 Linking..."
    ld -m elf_i386 -T linker.ld -o "$KERNEL_BIN" $obj_list
    
    if [ -f "$KERNEL_BIN" ]; then
        local size=$(stat -c%s "$KERNEL_BIN" 2>/dev/null || stat -f%z "$KERNEL_BIN" 2>/dev/null)
        status "Kernel linked: ${size} bytes"
    else
        error "Kernel linking failed"
    fi
}

create_iso() {
    log "Creating ISO for CD-RW..."
    
    cat > iso_root/boot/grub/grub.cfg << EOL
set timeout=5
set default=0
menuentry "WNKA OS" { multiboot /boot/kernel.bin; boot }
menuentry "WNKA OS (Debug)" { multiboot /boot/kernel.bin debug; boot }
menuentry "Reboot" { reboot }
menuentry "Shutdown" { halt }
EOL
    
    grub-mkrescue -o "$ISO_NAME" iso_root 2>/dev/null || {
        grub-mkrescue --output="$ISO_NAME" iso_root || error "Failed to create ISO"
    }
    
    local size=$(du -h "$ISO_NAME" | cut -f1)
    status "ISO created: $ISO_NAME ($size)"
    info "💿 Ready for CD-RW: $ISO_NAME"
}

create_usb_image() {
    log "Creating USB image (for flash drive)..."
    
    # Создаем образ размером 64MB
    info "Creating 64MB USB image..."
    dd if=/dev/zero of="$IMG_NAME" bs=1M count=64 2>/dev/null
    
    # Создаем раздел MBR
    parted "$IMG_NAME" mklabel msdos 2>/dev/null
    parted "$IMG_NAME" mkpart primary fat32 1MB 100% 2>/dev/null
    parted "$IMG_NAME" set 1 boot on 2>/dev/null
    
    # Настраиваем loop устройство
    LOOP_DEV=$(sudo losetup --find --show --partscan "$IMG_NAME")
    
    # Форматируем как FAT32
    info "Formatting as FAT32..."
    sudo mkfs.vfat -F 32 -n "WNKA_OS" "${LOOP_DEV}p1" >/dev/null 2>&1
    
    # Монтируем и копируем файлы
    info "Copying files to USB image..."
    mkdir -p mnt_point
    sudo mount "${LOOP_DEV}p1" mnt_point
    
    sudo mkdir -p mnt_point/boot/grub
    sudo cp -r iso_root/boot/* mnt_point/boot/
    
    # Устанавливаем GRUB
    info "Installing GRUB..."
    sudo grub-install --target=i386-pc --boot-directory=mnt_point/boot --force --recheck "$LOOP_DEV" >/dev/null 2>&1
    
    # Создаем GRUB конфиг
    sudo tee mnt_point/boot/grub/grub.cfg > /dev/null << EOL
set timeout=5
set default=0
menuentry "WNKA OS (from USB)" { multiboot /boot/kernel.bin; boot }
menuentry "WNKA OS (Debug Mode)" { multiboot /boot/kernel.bin debug; boot }
menuentry "Reboot" { reboot }
menuentry "Shutdown" { halt }
EOL
    
    # Размонтируем и очищаем
    sudo umount mnt_point
    sudo losetup -d "$LOOP_DEV"
    rmdir mnt_point
    
    local size=$(du -h "$IMG_NAME" | cut -f1)
    status "USB image created: $IMG_NAME ($size)"
    info "💾 Ready for flash drive: $IMG_NAME"
}

burn_cd() {
    local device="$1"
    
    if [ ! -e "$device" ]; then
        error "Device $device not found!"
    fi
    
    info "💿 Burning to CD-RW: $device"
    info "This will ERASE the CD-RW and write WNKA OS"
    read -p "Type 'YES' to continue: " confirm
    
    if [ "$confirm" != "YES" ]; then
        info "CD burning cancelled"
        return
    fi
    
    if [[ "$device" == *"sr"* ]] || [[ "$device" == *"cd"* ]]; then
        # Для CD-RW
        sudo wodim -v dev="$device" -eject blank=fast "$ISO_NAME"
    else
        # Для обычного прожига
        sudo cdrecord -v dev="$device" "$ISO_NAME"
    fi
    
    if [ $? -eq 0 ]; then
        success "CD burned successfully!"
        info "CD ejected - you can now boot from it"
    else
        error "CD burning failed"
    fi
}

write_usb() {
    local device="$1"
    
    if [ ! -b "$device" ]; then
        error "Not a block device: $device"
    fi
    
    # Проверяем, не является ли устройство системным диском
    if [[ "$device" == *"sda"* ]] || [[ "$device" == *"nvme0"* ]]; then
        warning "⚠️  This looks like a system disk!"
        read -p "Are you ABSOLUTELY sure? Type 'I AM SURE': " confirm
        if [ "$confirm" != "I AM SURE" ]; then
            info "USB write cancelled"
            return
        fi
    fi
    
    info "💾 Writing to USB: $device"
    info "This will DESTROY ALL DATA on $device!"
    
    # Показываем информацию об устройстве
    echo "Device info:"
    sudo fdisk -l "$device" | head -5
    
    read -p "Type 'YES' to continue: " confirm
    
    if [ "$confirm" != "YES" ]; then
        info "USB write cancelled"
        return
    fi
    
    # Размонтируем все разделы если они примонтированы
    sudo umount "$device"?* 2>/dev/null || true
    
    # Записываем образ
    log "Writing to USB, please wait..."
    sudo dd if="$IMG_NAME" of="$device" bs=1M status=progress conv=fsync
    
    if [ $? -eq 0 ]; then
        success "USB write completed successfully!"
        info "You can now boot from this USB drive"
        info "To safely remove: sync && sudo eject $device"
    else
        error "USB write failed"
    fi
}

run_qemu_cd() {
    log "Starting QEMU (CD mode)..."
    qemu-system-i386 -cdrom "$ISO_NAME" -m 256 -vga std -soundhw pcspk -machine pc
}

run_qemu_usb() {
    log "Starting QEMU (USB mode)..."
    if [ ! -f "$IMG_NAME" ]; then
        warning "USB image not found, creating first..."
        create_usb_image
    fi
    qemu-system-i386 -hda "$IMG_NAME" -m 256 -vga std -soundhw pcspk -machine pc
}

show_summary() {
    local end_time=$(date +%s)
    local elapsed=$((end_time - START_TIME))
    local obj_count=$(echo $OBJECTS | wc -w)
    
    echo "==============================================="
    echo "           BUILD COMPLETED SUCCESSFULLY"
    echo "==============================================="
    echo "  ISO (CD):  $(pwd)/$ISO_NAME"
    if [ -f "$IMG_NAME" ]; then
        echo "  IMG (USB): $(pwd)/$IMG_NAME"
    fi
    echo "  Kernel:     $(du -h $KERNEL_BIN 2>/dev/null | cut -f1)"
    echo "  Objects:    $obj_count files"
    echo "  Time:       $((elapsed/60))m $((elapsed%60))s"
    echo "  Log:        $LOG_FILE"
    echo "==============================================="
    echo ""
    echo "📋 NEXT STEPS:"
    echo ""
    echo "💿 For CD-RW:"
    echo "  ./build.sh --burn-cd /dev/sr0     # Burn to CD-RW"
    echo "  ./build.sh --run-cd                # Test in QEMU"
    echo ""
    echo "💾 For USB Flash:"
    echo "  ./build.sh --usb                    # Create USB image"
    echo "  ./build.sh --write-usb /dev/sdb     # Write to USB"
    echo "  ./build.sh --run-usb                 # Test USB in QEMU"
    echo "==============================================="
}

show_help() {
    cat << EOL
🚀 WNKA OS Build System v2.0 - Multi-Platform
================================================

Usage: $0 [OPTIONS]

BUILD OPTIONS:
  -c, --clean          Clean build directory
  -h, --help           Show this help

MEDIA CREATION:
  --usb                Create USB image (after build)
  --burn-cd DEVICE     Burn ISO to CD-RW (e.g., /dev/sr0)
  --write-usb DEVICE   Write image to USB (e.g., /dev/sdb)

TEST OPTIONS:
  -r, --run            Run in QEMU (CD mode)
  --run-cd             Run in QEMU (CD mode)
  --run-usb            Run in QEMU (USB mode)

EXAMPLES:
  # Build only
  ./wnka_iso_creator_tool.sh

  # Build and create USB image
  ./wnka_iso_creator_tool.sh --usb

  # Build and burn to CD
  ./wnka_iso_creator_tool.sh --burn-cd /dev/sr0

  # Build and write to USB
  ./wnka_iso_creator_tool.sh --write-usb /dev/sdb

  # Build and test in QEMU
  ./wnka_iso_creator_tool.sh --run-usb

  # Full workflow: clean, build, create USB, test
  ./wnka_iso_creator_tool.sh --clean --usb --run-usb

================================================
EOL
    exit 0
}

# Parse arguments
CLEAN=0
RUN_MODE=""
CREATE_USB=0
BURN_CD=""
WRITE_USB=""

while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--clean) CLEAN=1; shift ;;
        -r|--run)   RUN_MODE="cd"; shift ;;
        --run-cd)   RUN_MODE="cd"; shift ;;
        --run-usb)  RUN_MODE="usb"; shift ;;
        --usb)      CREATE_USB=1; shift ;;
        --burn-cd)  BURN_CD="$2"; shift 2 ;;
        --write-usb) WRITE_USB="$2"; shift 2 ;;
        -h|--help)  show_help ;;
        *) echo "❌ Unknown option: $1"; show_help ;;
    esac
done

# Main build process
echo "==============================================="
echo "   WNKA OS - PROFESSIONAL BUILD SYSTEM v2.0"
echo "   Multi-Platform: CD-RW & USB Support"
echo "==============================================="

check_deps

if [ $CLEAN -eq 1 ]; then
    clean
    if [ -z "$RUN_MODE" ] && [ $CREATE_USB -eq 0 ] && [ -z "$BURN_CD" ] && [ -z "$WRITE_USB" ]; then
        exit 0
    fi
fi

# Build if we need to (unless only burning/writing without build)
if [ ! -f "$KERNEL_BIN" ] || [ $CREATE_USB -eq 1 ] || [ -n "$RUN_MODE" ]; then
    # Компилируем всё
    compile_boot
    compile_kernel
    compile_shell
    compile_drivers
    compile_fs
    compile_resource_monitor
    compile_themes
    link_kernel
    create_iso
fi

# Create USB image if requested
if [ $CREATE_USB -eq 1 ]; then
    if [ ! -d "iso_root" ]; then
        error "ISO root not found. Build first!"
    fi
    create_usb_image
fi

# Show summary
show_summary

# Burn to CD if requested
if [ -n "$BURN_CD" ]; then
    if [ ! -f "$ISO_NAME" ]; then
        error "ISO not found. Build first!"
    fi
    burn_cd "$BURN_CD"
fi

# Write to USB if requested
if [ -n "$WRITE_USB" ]; then
    if [ ! -f "$IMG_NAME" ]; then
        info "USB image not found, creating first..."
        create_usb_image
    fi
    write_usb "$WRITE_USB"
fi

# Run in QEMU if requested
if [ -n "$RUN_MODE" ]; then
    case "$RUN_MODE" in
        cd)  run_qemu_cd ;;
        usb) run_qemu_usb ;;
    esac
fi
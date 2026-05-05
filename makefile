CXX = g++
CXXFLAGS = -m32 -ffreestanding -fno-stack-protector -fno-exceptions -fno-rtti -std=c++11 \
           -I. -Iinclude -Idrivers -Ifs -Ikernel -Ithemes -Isys -Iutils \
           -g -O0
ASM = nasm
ASMFLAGS = -f elf32
LD = ld
LDFLAGS = -m elf_i386 -T linker.ld
CC = gcc
CFLAGS = -m32 -ffreestanding -nostdlib -I. -Isys

BUILD_DIR = build
ISO_DIR = iso_root
BOOT_ISO_DIR = $(ISO_DIR)/boot
GRUB_DIR = $(BOOT_ISO_DIR)/grub
KERNEL_BIN = $(BOOT_ISO_DIR)/kernel.bin
ISO_NAME = wnka32.iso

# В OBJS добавь arp.o
OBJS = $(BUILD_DIR)/boot.o \
       $(BUILD_DIR)/kernel.o \
       $(BUILD_DIR)/shell.o \
       $(BUILD_DIR)/ata.o \
       $(BUILD_DIR)/ahci.o \
       $(BUILD_DIR)/wnkfs.o \
       $(BUILD_DIR)/mouse.o \
       $(BUILD_DIR)/themes.o \
       $(BUILD_DIR)/screensaver.o \
       $(BUILD_DIR)/resource_monitor.o \
       $(BUILD_DIR)/scheduler.o \
       $(BUILD_DIR)/install.o \
       $(BUILD_DIR)/e1000.o \
       $(BUILD_DIR)/dos_emu.o \
       $(BUILD_DIR)/ramfs.o \
       $(BUILD_DIR)/tcc.o \
       $(BUILD_DIR)/ide.o \
       $(BUILD_DIR)/wnkui.o \
       $(BUILD_DIR)/vga256.o \
       $(BUILD_DIR)/sounds.o \
       $(BUILD_DIR)/multitask.o \
       $(BUILD_DIR)/paint.o \
       $(BUILD_DIR)/piano.o \
       $(BUILD_DIR)/exception.o \
       $(BUILD_DIR)/memory_protect.o \
       $(BUILD_DIR)/watchdog.o \
       $(BUILD_DIR)/crashme.o \
       $(BUILD_DIR)/icmp.o \
       $(BUILD_DIR)/arp.o \
	   $(BUILD_DIR)/fdc.o \
	   $(BUILD_DIR)/floppy.o \
	   $(BUILD_DIR)/wnkc.o \
	   $(BUILD_DIR)/cdrom.o \
	   $(BUILD_DIR)/elf_linux.o \
	   $(BUILD_DIR)/vfs_linux.o \
	   $(BUILD_DIR)/syscall_linux.o \
	   $(BUILD_DIR)/wnvesa.o \
	   $(BUILD_DIR)/http.o

.PHONY: all clean run info dirs fix-includes install-deps

all: dirs $(OBJS) $(BUILD_DIR)/kernel.bin $(ISO_NAME)
	@echo "✅ Сборка завершена"
	@$(MAKE) info

dirs:
	@mkdir -p $(BUILD_DIR) $(ISO_DIR) $(BOOT_ISO_DIR) $(GRUB_DIR)
	@echo "📁 Директории созданы"

$(BUILD_DIR)/boot.o: boot/boot.asm
	@echo "🔨 Компиляция boot.asm..."
	$(ASM) $(ASMFLAGS) $< -o $@

$(BUILD_DIR)/kernel.o: boot/kernel.cpp
	@echo "🔨 Компиляция kernel.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/shell.o: sys/shell.cpp
	@echo "🔨 Компиляция shell.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/cdrom.o: sys/cdrom.cpp
	@echo "🔨 Компиляция cdrom.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/ata.o: drivers/ata.cpp
	@echo "🔨 Компиляция ata.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/ahci.o: drivers/ahci.cpp
	@echo "🔨 Компиляция ahci.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/wnkfs.o: wnkfs.cpp
	@echo "🔨 Компиляция wnkfs.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/mouse.o: drivers/mouse.cpp
	@echo "🔨 Компиляция mouse.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/themes.o: themes/themes.cpp
	@echo "🔨 Компиляция themes.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/screensaver.o: themes/screensaver.cpp
	@echo "🔨 Компиляция screensaver.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/resource_monitor.o: themes/resource_monitor.cpp
	@echo "🔨 Компиляция resource_monitor.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/scheduler.o: sys/scheduler.cpp
	@echo "🔨 Компиляция scheduler.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/install.o: sys/install.cpp
	@echo "🔨 Компиляция install.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/e1000.o: sys/e1000.cpp
	@echo "🔨 Компиляция e1000.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/dos_emu.o: sys/dos_emu.cpp
	@echo "🔨 Компиляция dos_emu.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@
	
$(BUILD_DIR)/ramfs.o: sys/ramfs.cpp
	@echo "🔨 Компиляция ramfs.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/tcc.o: sys/tcc.cpp
	@echo "🔨 Компиляция (tiny-c) tcc.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/ide.o: sys/ide.cpp
	@echo "🔨 Компиляция ide.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/wnkui.o: sys/wnkui.cpp
	@echo "🔨 Компиляция wnkui.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/vga256.o: sys/vga256.cpp
	@echo "🔨 Компиляция vga256.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/sounds.o: sys/sounds.cpp
	@echo "🔨 Компиляция sounds.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/multitask.o: sys/multitask.cpp
	@echo "🔨 Компиляция multitask.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/paint.o: sys/paint.cpp
	@echo "🔨 Компиляция paint.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/piano.o: sys/piano.cpp
	@echo "🔨 Компиляция piano.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/exception.o: sys/exception.cpp
	@echo "🔨 Компиляция exception.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/memory_protect.o: sys/memory_protect.cpp
	@echo "🔨 Компиляция memory_protect.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/watchdog.o: sys/watchdog.cpp
	@echo "🔨 Компиляция watchdog.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/crashme.o: sys/crashme.cpp
	@echo "🔨 Компиляция crashme.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/icmp.o: sys/icmp.cpp
	@echo "🔨 Компиляция icmp.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/arp.o: sys/arp.cpp
	@echo "🔨 Компиляция arp.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/fdc.o: sys/fdc.cpp
	@echo "🔨 Компиляция fdc.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/floppy.o: sys/floppy.cpp
	@echo "🔨 Компиляция floppy.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/wnkc.o: sys/wnkc.cpp
	@echo "🔨 Компиляция wnkc.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/http.o: sys/http.cpp
	@echo "🔨 Компиляция http.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/syscall_linux.o: sys/syscall_linux.cpp
	@echo "🔨 Компиляция syscall_linux.cpp (Linux эмуляция)..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/elf_linux.o: sys/elf_linux.cpp
	@echo "🔨 Компиляция elf_linux.cpp (загрузчик ELF)..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/vfs_linux.o: sys/vfs_linux.cpp
	@echo "🔨 Компиляция vfs_linux.cpp (виртуальная ФС Linux)..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/wnvesa.o: sys/wnvesa.cpp
	@echo "🔨 Компиляция wnvesa.cpp..."
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/kernel.bin: $(OBJS)
	@echo "🔗 Линковка ядра..."
	$(LD) $(LDFLAGS) $^ -o $@
	@echo "✅ Ядро собрано: $(shell du -h $@ | cut -f1)"

$(ISO_NAME): $(BUILD_DIR)/kernel.bin
	@echo "💿 Создание ISO..."
	@cp $(BUILD_DIR)/kernel.bin $(BOOT_ISO_DIR)/
	@echo "set timeout=0" > $(GRUB_DIR)/grub.cfg
	@echo "set default=0" >> $(GRUB_DIR)/grub.cfg
	@echo "menuentry 'WNKA OS' { multiboot /boot/kernel.bin; boot }" >> $(GRUB_DIR)/grub.cfg
	@grub-mkrescue -o $(ISO_NAME) $(ISO_DIR) 2>/dev/null && \
		echo "✅ ISO создан: $(ISO_NAME) ($(shell du -h $(ISO_NAME) | cut -f1))" || \
		(echo "❌ Ошибка создания ISO. Установи: sudo apt install grub-pc-bin xorriso" && exit 1)


run: $(ISO_NAME)
	@echo "🚀 Запуск в QEMU со звуком и сетью..."
	qemu-system-i386 -cdrom $(ISO_NAME) -drive file=disk.img,format=raw -m 256 -vga std \
		-netdev user,id=net0,hostfwd=tcp::8080-:80 \
		-device e1000,netdev=net0 \
		-audiodev pa,id=snd0 -machine pcspk-audiodev=snd0 -boot d

run-debug-net: $(ISO_NAME)
	@echo "🔍 Запуск в QEMU (режим отладки с сетью)..."
	qemu-system-i386 -cdrom $(ISO_NAME) -hda disk.img -m 256 -vga std \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-soundhw pcspk -machine pc -d int -no-reboot

run-net: $(ISO_NAME)
	@echo "🌐 Запуск в QEMU (только сеть)..."
	qemu-system-i386 -cdrom $(ISO_NAME) -hda disk.img -m 256 -vga std \
		-netdev user,id=net0 \
		-device e1000,netdev=net0 \
		-boot d

run-hda: $(ISO_NAME)
	@echo "🚀 Запуск в QEMU с disk..."
	qemu-system-i386 -hda disk.img -m 256 -vga std -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0 -boot d

run-debug: $(ISO_NAME)
	@echo "🔍 Запуск в QEMU (режим отладки)..."
	qemu-system-i386 -cdrom $(ISO_NAME) -hda disk.img -m 256 -vga std -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0 -d int -no-reboot -boot d

run-debug-hda: $(ISO_NAME)
	@echo "🔍 Запуск в QEMU (режим отладки)..."
	qemu-system-i386 -hda disk.img -m 256 -vga std -audiodev pa,id=snd0 -machine pcspk-audiodev=snd0 -d int -no-reboot -boot d

run-usb: $(BUILD_DIR)/kernel.bin
	@echo "💾 Запуск USB режима в QEMU..."
	@dd if=/dev/zero of=usb.img bs=1M count=64 2>/dev/null
	@echo "✅ USB образ создан"
	qemu-system-i386 -hda usb.img -cdrom $(ISO_NAME) -m 256 -vga std -soundhw pcspk -machine pc

run-stable: $(ISO_NAME)
	@echo "🛡️ Запуск в QEMU (с защитой от крашей)..."
	qemu-system-i386 -cdrom $(ISO_NAME) -m 256 -vga std -no-reboot -no-shutdown

clean:
	@echo "🧹 Очистка..."
	rm -rf $(BUILD_DIR) $(ISO_DIR) $(ISO_NAME) *.img *.o
	find . -name "*.o" -type f -delete
	find . -name "*.a" -type f -delete
	find . -name "*.so" -type f -delete
	@echo "✅ Очищено"

clean-all: clean
	@echo "🧹 Полная очистка (включая зависимости)..."
	rm -rf iso_root build *.img *.iso
	@echo "✅ Полностью очищено"

info:
	@echo ""
	@echo "📊 СТАТИСТИКА СБОРКИ:"
	@echo "========================"
	@if [ -f $(BUILD_DIR)/kernel.bin ]; then \
		echo "Ядро:     $$(du -h $(BUILD_DIR)/kernel.bin | cut -f1)"; \
	fi
	@if [ -f $(ISO_NAME) ]; then \
		echo "ISO:      $$(du -h $(ISO_NAME) | cut -f1)"; \
	fi
	@echo "Объектов: $$(find $(BUILD_DIR) -name "*.o" 2>/dev/null | wc -l) файлов"
	@echo ""
	@echo "📁 Исходные файлы:"
	@echo "  boot/     : $$(ls -la boot/ 2>/dev/null | grep -E "\.(asm|cpp)" | wc -l) файлов"
	@echo "  drivers/  : $$(ls -la drivers/ 2>/dev/null | grep -E "\.(cpp|h)" | wc -l) файлов"
	@echo "  fs/       : $$(ls -la fs/ 2>/dev/null | grep -E "\.(cpp|h)" | wc -l) файлов"
	@echo "  sys/      : $$(ls -la sys/ 2>/dev/null | grep -E "\.(cpp|h)" | wc -l) файлов"
	@echo "  themes/   : $$(ls -la themes/ 2>/dev/null | grep -E "\.(cpp|h)" | wc -l) файлов"
	@echo "  utils/    : $$(ls -la utils/ 2>/dev/null | grep -E "\.(cpp|h)" | wc -l) файлов"
	@echo "========================"
	@echo ""
	@echo "🛡️ ЗАЩИТА СИСТЕМЫ:"
	@echo "  ✅ Exception handlers - Перехват ошибок"
	@echo "  ✅ Memory protection - Защита памяти"
	@echo "  ✅ Watchdog - Сторожевой таймер"
	@echo "  ✅ Safe I/O - Безопасный ввод/вывод"
	@echo "  ✅ Recovery - Точки восстановления"

install-deps:
	@echo "📦 Установка зависимостей..."
	sudo apt update
	sudo apt install -y g++ nasm grub-pc-bin xorriso qemu-system-x86 mtools
	@echo "✅ Зависимости установлены"

fix-includes:
	@echo "🔧 Исправление include paths..."
	@for dir in drivers fs themes sys utils; do \
		if [ -d "$$dir" ]; then \
			cd "$$dir" && \
			for h in ../drivers/*.h; do \
				if [ -f "$$h" ] && [ ! -f "$$(basename $$h)" ]; then \
					ln -sf "$$h" ./ 2>/dev/null && echo "  ✅ $$(basename $$h) -> drivers/"; \
				fi \
			done && \
			cd ..; \
		fi \
	done
	@echo "✅ Include paths исправлены"

check:
	@echo "🔍 Проверка структуры проекта..."
	@for file in boot/boot.asm boot/kernel.cpp drivers/ata.cpp drivers/ahci.cpp drivers/mouse.cpp wnkfs.cpp sys/shell.cpp sys/exception.cpp sys/memory_protect.cpp sys/watchdog.cpp; do \
		if [ -f "$$file" ]; then \
			echo "  ✅ $$file"; \
		else \
			echo "  ❌ $$file (ОТСУТСТВУЕТ!)"; \
		fi \
	done
	@echo ""
	@echo "🔍 Проверка заголовочных файлов..."
	@for file in drivers/video.h drivers/graph.h fs/wnkfs.h fs/fs.h themes/themes.h sys/exception.h sys/memory_protect.h sys/watchdog.h; do \
		if [ -f "$$file" ]; then \
			echo "  ✅ $$file"; \
		else \
			echo "  ❌ $$file (ОТСУТСТВУЕТ!)"; \
		fi \
	done

help:
	@echo "📚 ДОСТУПНЫЕ КОМАНДЫ:"
	@echo "========================"
	@echo "make          - полная сборка"
	@echo "make run      - запуск в QEMU"
	@echo "make run-debug - запуск с отладкой"
	@echo "make run-stable - запуск с защитой"
	@echo "make run-usb  - тест USB режима"
	@echo "make clean    - очистка"
	@echo "make clean-all - полная очистка"
	@echo "make info     - информация о сборке"
	@echo "make check    - проверка структуры"
	@echo "make fix-includes - исправить include paths"
	@echo "make install-deps - установить зависимости"
	@echo "========================"

.DEFAULT_GOAL := all
.ONESHELL:
.NOTPARALLEL:
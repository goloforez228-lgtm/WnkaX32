echo "🔗 Создание ссылок на заголовочные файлы..."

# Папки куда будем ставить ссылки
TARGET_DIRS="sys kernel drivers themes fs"

# Папки откуда брать .h
SOURCE_DIRS="drivers themes fs kernel"

for target in $TARGET_DIRS; do
    if [ -d "$target" ]; then
        echo "📁 Обработка $target/"
        cd "$target"
        
        # Для каждой исходной папки
        for src in $SOURCE_DIRS; do
            if [ -d "../$src" ]; then
                for h in ../$src/*.h; do
                    if [ -f "$h" ]; then
                        ln -sf "$h" ./ 2>/dev/null
                        echo "  ✅ $(basename $h) -> ../$src/$(basename $h)"
                    fi
                done
            fi
        done
        cd ..
    fi
done

echo "✅ Готово!"
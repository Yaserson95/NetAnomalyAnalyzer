#!/bin/bash
# Проверка Linux-пакетов (DEB/RPM) собранного проекта.
#
# Запускать от root (например, в WSL Debian):
#   bash tools/linux/verify_packages.sh build-linux
#
# Проверяет: метаданные, состав, установку DEB, библиотечные зависимости
# (ldd), краткий запуск GUI в offscreen-режиме, работу каталогов
# /etc/netanalyzer и /var/log/netanalyzer, удаление пакета.
set -u

BUILD_DIR="${1:-build-linux}"
DEB="$BUILD_DIR/netanalyzer_1.0.0_amd64.deb"
RPM="$BUILD_DIR/netanalyzer-1.0.0-1.x86_64.rpm"
FAIL=0

step() { echo; echo "=== $*"; }
ok()   { echo "  [OK] $*"; }
bad()  { echo "  [FAIL] $*"; FAIL=1; }

[ "$(id -u)" = "0" ] || { echo "нужны права root"; exit 1; }
[ -f "$DEB" ] || { echo "нет пакета: $DEB"; exit 1; }

step "Метаданные DEB"
dpkg-deb -I "$DEB" | sed -n '2,16p'

step "Зависимости RPM (rpm -qp --requires)"
if [ -f "$RPM" ]; then
    rpm -qp --requires "$RPM" || bad "rpm -qp --requires"
    rpm -qp --vendor --license "$RPM" 2>/dev/null
else
    echo "  (RPM не найден — пропуск)"
fi

step "Установка DEB"
apt-get install -y -qq -o Dpkg::Options::=--force-confnew "./$DEB" >/tmp/pkg_install.log 2>&1
if [ $? -eq 0 ]; then ok "установлен"; else bad "установка DEB"; tail -5 /tmp/pkg_install.log; fi

step "Состав установленного пакета"
dpkg -L netanalyzer | grep -E "bin/netanalyzer|\.desktop|README|etc/netanalyzer|var/log/netanalyzer" || bad "состав"

step "Симлинки и запуск через /usr/bin"
if [ -L /usr/bin/netanalyzer ] && [ -e /usr/bin/netanalyzer ]; then
    ok "/usr/bin/netanalyzer -> $(readlink /usr/bin/netanalyzer)"
else
    bad "симлинк /usr/bin/netanalyzer"
fi

step "ldd: разрешение всех библиотек"
if ldd /usr/lib/netanalyzer/NetAnomalyAnalyzer | grep "not found"; then
    bad "есть неразрешённые библиотеки (см. выше)"
else
    ok "все библиотеки разрешены"
fi
if ldd /usr/lib/netanalyzer/NetAnalyzerService | grep "not found"; then
    bad "service: неразрешённые библиотеки"
else
    ok "service: все библиотеки разрешены"
fi

step "RUNPATH бинарников и плагинов"
readelf -d /usr/lib/netanalyzer/NetAnomalyAnalyzer | grep -E "RPATH|RUNPATH" || echo "  (нет RUNPATH у exe)"
readelf -d /usr/lib/netanalyzer/platforms/libqxcb.so | grep -E "RPATH|RUNPATH" || bad "нет RUNPATH у libqxcb.so"

step "Краткий запуск GUI (offscreen, 8 с)"
timeout 8 env QT_QPA_PLATFORM=offscreen NETAN_CONFIG_DIR=/tmp/pkg-smoke-cfg \
    /usr/lib/netanalyzer/NetAnomalyAnalyzer >/tmp/gui_smoke.log 2>&1
RC=$?
if [ "$RC" = "124" ] || [ "$RC" = "0" ]; then
    ok "процесс запустился и не упал (exit=$RC)"
else
    bad "GUI упал с exit=$RC"; tail -5 /tmp/gui_smoke.log
fi

step "Каталоги конфигурации и журналов созданы пакетом"
[ -d /etc/netanalyzer ] && ok "/etc/netanalyzer" || bad "нет /etc/netanalyzer"
[ -d /var/log/netanalyzer ] && ok "/var/log/netanalyzer" || bad "нет /var/log/netanalyzer"

step "Удаление пакета (каталоги данных должны убираться, если пусты)"
apt-get remove -y -qq netanalyzer >/tmp/pkg_remove.log 2>&1
if [ $? -eq 0 ]; then ok "удалён"; else bad "удаление"; fi
if [ -e /usr/bin/netanalyzer ]; then bad "симлинк остался"; else ok "симлинк удалён"; fi
if [ -d /etc/netanalyzer ]; then echo "  /etc/netanalyzer остался (в нём файлы — нормально)"; else ok "/etc/netanalyzer убран dpkg"; fi

echo
if [ "$FAIL" = "0" ]; then echo "ВСЕ ПРОВЕРКИ ПРОЙДЕНЫ"; else echo "ЕСТЬ ОШИБКИ (см. [FAIL])"; fi
exit "$FAIL"

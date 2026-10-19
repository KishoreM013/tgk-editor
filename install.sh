#!/usr/bin/env bash
# Build tgk editor and install it for the current user (~/.local).
set -e
cd "$(dirname "$0")"

cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"

# clean up older installs (tgk cpp era)
rm -f ~/.local/bin/tgkcpp \
      ~/.local/share/applications/tgkcpp.desktop \
      ~/.local/share/icons/hicolor/256x256/apps/tgkcpp.png

mkdir -p ~/.local/bin ~/.local/share/applications \
         ~/.local/share/icons/hicolor/256x256/apps

cp build/tgkeditor ~/.local/bin/

# render + install the icon
./build/icon_gen ~/.local/share/icons/hicolor/256x256/apps/tgkeditor.png

sed "s|@EDITOR@|$PWD/build/tgkeditor|" cppide.desktop \
    > ~/.local/share/applications/tgkeditor.desktop
update-desktop-database ~/.local/share/applications 2>/dev/null || true

echo
echo "Installed as 'tgk editor'. Run 'tgkeditor' from a terminal, launch it"
echo "from your app menu, or open files with:  tgkeditor myfile.java"

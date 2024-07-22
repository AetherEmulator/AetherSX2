# AetherSX2

### NEW Updates

AetherSX2 development is indefinitely suspended.
Due to neverending impersonating, complaints, demands, and now death threats, I'm done.

You can still download/use the app and it will continue to work for the forseeable future.

AetherSX2 was always meant to be a fun hobby for me, not profit driven. It doesn't make sense to continue working on a hobby which isn't fun anymore.

Stay safe out there, and watch out for scammers, there seems to be a lot of them.

(e.g. there's multiple people claiming to represent AetherSX2 on various social media - they are not legit)

Thanks to everyone who wasn't a d*ck for the last year.

Current build downloads are still available at https://www.aethersx2.com/archive/ - please follow good security hygiene and don't install APKs from random sources.

Update 2023/01/10: I no longer have any active online presence in any community. Anyone claiming to be Tahlreth, or represent AetherSX2, is impersonating and scamming you.
I hope the threats and hate will stop now.

---

## Build Instructions

Please note that this is *not* a full source release for the library, only the LGPL sources, and parts of which that have been modified.
Releasing full source code is *not* required by the LGPL, only that the closed source components can be re-linked/combined with
the LGPL components and build instructions included as per section 4/5 of the license.

An apk suitable for injecting the recompiled library is provided as a convenience, as the library cannot be used outside
of the Android app without modification.

You will need:
 - Android SDK, Build Tools and NDK. We use SDK 31, build-tools 31 and NDK 23.1.7779620.
 - A Linux machine. We used Ubuntu 20.04.3 LTS.
 - CMake 3.22.0. You can get this from the Kitware repository for Ubuntu distros.

You should have:
 - app-release-unsigned.apk: Unsigned APK containing compiled Java code without native library.
 - aethersx2-libemucore.tar.xz: Archive of source code.

We have verified that the library can be re-linked/reproduced on the above system configuration. If you use a different configuration,
it is not guaranteed to be functional.

Build steps:

1. Clone repo
    ```bash
    git clone https://github.com/AetherSX2/AetherSX2.git
    cd AetherSX2
    ```
2. Create a build directory for the native library and change into it:
    ```bash
    mkdir build-android
    cd build-android
    ```
3. Configure the build system. Change PATH_TO_NDK to whereever the NDK is installed:
   ```bash
    cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=/PATH_TO_NDK/build/cmake/android.toolchain.cmake -DANDROID_PLATFORM=android-26 -DANDROID_ABI=arm64-v8a ..
   ```
4. Compile the native library: 
   ```bash
   make -j16
   ```
5. Create a directory for packaging the APK:
   ```bash
   mkdir apk
   cd apk
   ```

6. Copy the skeleton APK to this directory:
   ```bash
   cp PATH_TO_app-release-unsigned.apk aethersx2.apk
   ```
7. Copy the native library into the correct location: 
   ```bash
   mkdir -p lib/arm64-v8a
   cp ../pcsx2/libemucore.so lib/arm64-v8a
   ````
8. Add the native library to the APK. zip is used instead of aapt because aapt will compress it:
   ```bash
   zip -0 aethersx2.apk lib/arm64-v8a/libemucore.so
   ```
9. Ensure the native library is aligned to a 4-byte boundary:
   ```bash
   zipalign -p 4 aethersx2.apk aethersx2-aligned.apk
   ```
10. Create a signing key for your build. Mark down the keystore password.
    ```bash
    keytool -genkey -v -keystore keyname.keystore -alias keyname -keyalg RSA -keysize 2048 -validity 10000
    ```
11. Sign the APK, replacing PASSWORD_TO_KEYSTORE with above: 
    ```bash
    apksigner sign --ks keyname.keystore --ks-pass "pass:PASSWORD_TO_KEYSTORE" --ks-key-alias keyname --out aethersx2-signed.apk --verbose aethersx2-aligned.apk
    ```

This will produce aethersx2-signed.apk, which can be installed on your device.

## FAQ

## What is it?
AetherSX2 is an emulator of the PS Two console for the Android platform. You can play games which you have dumped from disc on your portable device.

## Where is the APK?
There is **no APK for download**. AetherSX2 is only distributed via the play store.

## What are the requirements?
The console is an **extremely** complex piece of hardware, with many very powerful components, even for today. You need a high end device to achieve good performance.

We recommend at least a Snapdragon 845-equivalent device. This means 4 large cores (Cortex-A75 level). If you only have two big cores (e.g. Snapdragon 700 series SoCs, you should not enable multi-threaded VU, and performance will suffer as a result). Devices with Mali or PowerVR GPUs will run the app, but performance will be much lower than Adreno GPUs, and the Vulkan renderer will not be available. This is because they are missing a critical feature (dual-source blending).

If you want to use the app on a slower device, you can try it, but games **will** run slow, especially heavier titles. You can try underclocking the CPU by setting the cycle rate to a negative number, and the cycle skip to a positive number in System settings, but this will cause games to lag internally at best, or crash at worst.

### Expectations.
This is a free app, worked on as a hobby in the developer's free time. At the time of writing, it is in very early stages and missing many useful features, however is usable for playing some games.

It is not going to be perfect, far from it. I will continue to improve it when I have time, but please remember this is not my job, and to have realistic expectations, **especially if you do not have a high end device**.

As it is an early preview, you should save your game regularly in case of crashes.

## Disclaimers.
AetherSX2 should only be used to play games you own and have dumped from disc yourself. To dump your games, you can use a tool such as ImgBurn to create a .iso for the disc, and then copy across it to your device over USB.

PlayStation 2 and PS2 are registered trademarks of Sony Interactive Entertainment Europe Limited and in other regions. This app is not affiliated in any way with Sony Interactive Entertainment.

### The app tells me I need a BIOS.

Yes, you do. A BIOS image **is required** to play games and is not optional. This image should be dumped from your own console, using a homebrew application. There are plenty of guides available online on how to dump your console's BIOS.

### My games are running slow/lagging.
Different games have very different hardware requirements, due to how much they utilized the various components of the console. You can try underclocking the CPU by setting the cycle rate to a negative number, and the cycle skip to a positive number in System settings, but this will cause games to lag internally at best, or crash at worst.

### How to improve performance?
- Make sure fastmem is enabled in System settings.
- Enable Multi-Threaded VU1 in System settings. This will cause lower performance if your device does not have at least three "big" CPU cores.
- Use the Vulkan renderer if you have an Adreno GPU. Note that some games will perform better with OpenGL, and may not render correctly with Vulkan. Mali GPUs are not supported by the Vulkan renderer.
- Underclock the emulated CPU by setting the cycle rate to a negative number, and cycle skip to a positive number in System settings.
- For some games, enabling the Preload Textures and GPU Palette conversion options in Graphics settings can improve performance.
- If the game slows down depending on the camera angle, this may be due to GS downloads, which are very slow on mobile GPUs. You can try disabling hardware readbacks in Graphics options, but this may create some glitches in effects.

### How do I customize the touchscreen controller (position/scale)?
Press the pause or back button while ingame, and tap the controls tab in the top-right corner. You can also add additional buttons for hotkeys here, e.g. fast forward, quick load/save, etc.

### The app opens in portrait mode, how do I change it to landscape?
Turn your device around if you have auto rotation enabled. You can also force it to always use landscape in the first page of App Settings.

### My Bluetooth controller isn't working.
Currently we only support a fixed controller mapping, which is determined by your Android vendor. Custom mappings for controllers are planned in the future.

### I want to set different settings for each game.
Long press the game in the game list/grid, tap Game Properties, and tap the Game Settings tab. If you want to change these settings while in-game, open the pause menu, and tap the info button in the top-right corner to access Game Properties.

### My games have rendering glitches.
Due to the complexity of the console's hardware, there are still plenty of issues which arise when using the hardware renderer. You can try using the software renderer for these games.

### I want to save more than one state.
Open the pause menu and tap Load/Save state, there are 10 slots + a quick save (for onscreen buttons).

### I want copy my saves from another device.
Currently you can only import an entire memory card at once; it is not possible to import individual saves. Swiping from the left in the game list will show an "Import Memory Card" option which you can use to import a *.ps2 image of a memory card.

### Where are my saves located?
Due to scoped storage on Android 11+, we cannot place your saves in a normal directory on external storage. However, with a file explorer app, you should be able to access the `Android/data/xyz.aethersx2.android` directory, in your primary storage volume, which contains your save states and memory cards. Note that accessing this directory requires granting additional permissions to your file manager on Android 11+.

### How do I add covers to the game grid?
Place cover images in the covers directory, located in the data directory mentioned above, with the file name as the game title or serial in jpg/png format. Alternatively, you can long press in the game list and select "Choose Cover Image" to import an image.

### How do I create launcher shortcuts for games?
Long press the game in the game list, and select "Create Launcher Shortcut".
# Star Trek Fleet Command - Community Mod

<p align="center">
  <img src="https://img.shields.io/badge/License-GPLv3-blue.svg" alt="License: GPLv3">
  <img src="https://img.shields.io/github/sponsors/netniv" alt="Sponsorship">
</p>

<p align="center">
   A community mod (patch) that adds a couple of tweaks to the mobile game <b>Star Trek Fleet Command&#8482;</b>
</p>

# Contributing

This project is entirely maintained in my own personal free time, and updated as and when that time
is available.  Any help by submitting pull requests is greatly welcome and any donations or
sponsorships are greatly appreciated.

## Building

First clone and initialize the repository:

```bash
git clone https://github.com/STFC-Mod/mod.git stfc-mod
cd stfc-mod
```

## Installing

Please note that when this project compiles, it will create a DLL called `stfc-community-mod.dll`.  This
file must be either copied to the `C:\Games\Star Trek Fleet Command\Star Trek Fleet Command\default\game`
folder as `version.dll` or create a symbolic link to the file using an elevated (administrator) command
prompt:

```console
cd C:\Games\Star Trek Fleet Command\Star Trek Fleet Command\default\game
mklink version.dll [output folder]\stfc-community-mod.dll
```

If you do link the file, please note you will need to close the game to recompile.

### Visual Studio

#### SDK Warning

You may see the following error, and this normally occurs if your SDK is below the minimum required.

```
The <experimental/coroutine> and <experimental/resumable> headers are only supported with /await and implement pre-C++20 coroutine support. Use <coroutine> for standard C++20 coroutines
```

If you have Visual Studio 2022, make sure to have the Windows 11 SDK selected as a minimum, even if you
are running the Visual Studio IDE on Windows 10.  The Windows 11 SDK will also create targets that work
for Windows 10.

You can do this via the Visual Studio Installer.  The Visual Studio Installer will show any other versions
of Visual Studio, so please make sure to use the `Modify` button on the correct one.  You can launch the
Visual Studio Installer from within Visual Studio 2022 via the `Tools -> Get Tools and Features` menu
option, or through `Settings -> Apps -> Visual Studio -> Modify`.

Once you are modifying the correct Visual Studio 2022 instance, select the `Individual Components` tab and
click into the Filter textbox.  Enter the type `SDK` which should provided a minimal filtered list of SDK's
available.  You should see Windows 11 SDK listed and has a tick.  If not, please click the tick box and then
apply the changes to have the SDK downloaded and installed.  This will take around 2-3GB of space.

#### Configure and building the project

Once they are installed, either standalone or via Visual Studio, you can configure the `stfc-mod` project.
We are using [XMake](https://xmake.io/#/).
To configure a Visual Studio solution, simple run the following on the Command Line.

```powershell
xmake project -k vsxmake -m "debug,release"
```
You will now find a `stfc-mod.sln` file inside `vsxmake2022`(or similarly named). You can simply open that in `Visual Studio`
and Build the solution.

**IMPORTANT**: To reset the build, you can remove the `build/` folder and all items beneath it.  Visual
Studio will then rebuild the project.

**IMPORTANT**: To fully reset the project, also remove the `.vs/` folder.

### Visual Studio Code

If you want to use Visual Studio Code, you may still need to make sure the various SDK's and MSVC runtimes
are available.

Once they are installed, either standalone or via Visual Studio, you can open the `stfc-mod` folder inside
Visual Studio or by right clicking in a Windows Explorer via and selecting `Open with Visual Studio Code`.
When it first opens, it should ask you to install the XMake extension.  Once the extensions are
installed, you can build the project by navigating to the XMake section in the Activity Bar and clicking `Build All` at the top.

**IMPORTANT**: To reset the build, you can remove the `build/` folder and all items beneath it.  Visual
Studio Code will then rebuild the project.

### Command Line

If you do not have Visual Studio Code, this project uses XMake, so the simplest way to build it on Windows:

```ps1
mkdir build
cd build
xmake
```

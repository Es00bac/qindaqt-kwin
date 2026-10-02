# qindaqt-kwin

QindaQt's compositor: KWin 6.6.6 plus QindaQt's own changes, with an identity
of its own so it installs and runs beside an unmodified KDE Plasma
(container-wm ADR-0291). This file maps what QindaQt changed and why. The
upstream base is recorded in [UPSTREAM.md](UPSTREAM.md).

## Identity

| What | Stock KWin | qindaqt-kwin |
|---|---|---|
| Program | `/usr/bin/kwin_wayland` (+ `kwin_wayland_wrapper`) | `/usr/bin/qindaqt-kwin`; no wrapper (the QindaQt session starts and restarts it) |
| Library | `libkwin.so.6` | `libqindaqt-kwin.so.0` |
| Headers, CMake | `/usr/include/kwin`, `KWin::kwin`, `KWinDBusInterface` | `/usr/include/qindaqt-kwin`, `QindaQtKWin::kwin` (exact version), `QindaQtKWinDBusInterface` |
| Version | 6.6.6 | 6.6.6.<serial> (`QINDAQT_KWIN_SERIAL` in `CMakeLists.txt`) |
| Plugins | `qt6/plugins/kwin/{plugins,effects}` | `qt6/plugins/qindaqt-kwin/{plugins,effects}` |
| Plugin interface ids | `org.kde.kwin.PluginFactoryInterface6.6.6` | `org.qindaqt.kwin.PluginFactoryInterface6.6.6.<serial>` (same for `EffectPluginFactory`) |
| Package structures | `KWin/Effect`, `/Script`, `/WindowSwitcher`, `/Decoration`, `/Aurorae` | `QindaQtKWin/Effect`, `/Script`, `/WindowSwitcher`, `/Decoration`; no Aurorae structure |
| QML modules | `org.kde.kwin*` | `org.qindaqt.kwin*` (and `org.qindaqt.KWin.Effect.WindowView`) |
| Data | `/usr/share/kwin-wayland` and the legacy `kwin/` roots | `/usr/share/qindaqt-kwin` only |
| Hung-window helper | `/usr/libexec/kwin_killer_helper`, `org.kde.kwin.killer`; KAuth and ksysguard's helper for processes of root or another user | `/usr/libexec/qindaqt-kwin/killer-helper`, `org.qindaqt.kwin.killer`; for processes of root or another user, pkexec runs `/usr/libexec/qindaqt-kwin/terminate-process` under the polkit action `org.qindaqt.kwin.terminate-process` (always `auth_admin`); no KAuth |
| Config (`$XDG_CONFIG_HOME`) | `kwinrc`, `kwinrulesrc`, `kwinoutputconfig.json`, `kcminputrc`, `kxkbrc` | QindaQt's config folder: `qindaqt/kwinrc`, `qindaqt/kwinrulesrc`, `qindaqt/kwinoutputconfig.json`, `qindaqt/kwininputrc`, `qindaqt/kwinxkbrc` |
| State (`$XDG_STATE_HOME`) | `kwinstaterc` | `qindaqt/kwinstaterc` |
| D-Bus | service `org.kde.KWin`, interfaces `org.kde.KWin*`, `org.kde.kwin.*`, paths `/KWin`, `/Effects`, `/org/kde/KWin/…` | service `org.qindaqt.KWin`, interfaces `org.qindaqt.KWin*`, paths under `/org/qindaqt/KWin`; installed XML `org.qindaqt.KWin*.xml` |
| Notifications, logging | `kwin.notifyrc`, `org_kde_kwin.categories` | `qindaqt-kwin.notifyrc` (component `qindaqt-kwin`), `qindaqt-kwin.categories` |
| Translation domain | `kwin` | `qindaqt-kwin` (no catalogs are installed) |
| Decoration library | shared `kde-plasma/kdecoration` (`libkdecorations3.so.6`, `libkdecorations3private.so.2`, `/usr/include/KDecoration3`, `KDecoration3`) | vendored in `kdecoration/`: `libqindaqt-kwin-decorations.so.0` and `…private.so.0`, `/usr/include/qindaqt-kwin/decoration`, CMake `QindaQtKWinDecoration` (exact version) |
| Decoration plugins | `qt6/plugins/org.kde.kdecoration3`, default `org.kde.breeze`, fallback Aurorae | `qt6/plugins/qindaqt-kwin/decorations` (`KDECORATION_PLUGIN_DIR`), default and fallback `org.qindaqt` (container-wm's decoration) |
| Privileged-client keys | `X-KDE-Wayland-Interfaces`, `X-KDE-DBUS-Restricted-Interfaces` | `X-QindaQt-KWin-Wayland-Interfaces`, `X-QindaQt-KWin-DBus-Restricted-Interfaces` |

Not built or installed: KCMs and the KRunner plugin (a configure error if
enabled), the handbook, translation catalogs, kconf_update migrations, the
systemd unit, the hicolor `kwin` icons, the kcfg files, `kwindowprop`.

### Names that stay on purpose

- Wire contracts: every Wayland protocol name (`org_kde_*`, `zkde_*`, `wp_*`,
  `xdg_*`, `ext_*`), `org.kde.kglobalaccel`, `org.freedesktop.*`,
  `org.kde.keyboard` (keyboard layouts), `org.kde.kappmenu`, the QPA plugin key
  `org.kde.kwin.qpa`, the effect metadata key `org.kde.kwin.effect`.
- The application and KGlobalAccel component name `kwin`: KF6GlobalAccel files
  KWin's shortcuts under it in `kglobalshortcutsrc`, shared until M6.
  `KConfig::setMainConfigName("qindaqt/kwinrc")` keeps the default config
  separate anyway. Qt's QML disk cache therefore still lands in
  `~/.cache/kwin/qmlcache`; its entries are keyed by source path, so the fork
  and stock KWin never read each other's.
- Shared configuration that is not KWin's: `kdeglobals`, `kaccessrc`,
  `klaunchrc`, `plasmarc`; `kglobalshortcutsrc`, `kscreenlockerrc` and
  `knighttimerc` leave with their services (M6, M2, M3).
- The C++ namespaces `KWin::` and `KDecoration3::`, the in-tree CMake aliases
  `KDecoration3::KDecoration` and `KDecoration3::KDecoration3Private`, the
  decoration config group `[org.kde.kdecoration2]` and the plugin metadata key
  `org.kde.kdecoration3`.
- PF21 removes the KDE service alias and the legacy Scripting objects. Screenshot,
  EIS, TabletModeManager and VirtualKeyboard use only `org.qindaqt.KWin*`
  interfaces and `/org/qindaqt/KWin` paths. Privileged clients must use QindaQt's
  desktop-file keys; stock KDE keys do not grant access. Deploy with the matching
  container-wm caller cutover and Gabbee native endpoint preference (ADR-0340).

A source line that must keep a stock name despite a rename rule carries the
comment marker `qindaqt-identity: keep`.

## QindaQt defaults

The fork's compiled-in defaults are QindaQt's, so a fresh QindaQt session needs
no seeding for them (container-wm ADR-0291):

| Setting | KWin default | qindaqt-kwin default | Why |
|---|---|---|---|
| Window decoration (`[org.kde.kdecoration2] library`) | `org.kde.breeze`, fallback Aurorae | `org.qindaqt`, also the fallback | QindaQt's own decoration |
| Window switcher (`[TabBox] LayoutName`) | `thumbnail_grid` | `qindaqt` with no bundled fallback | QindaQt's switcher from container-wm is required |
| `[Windows] ElectricBorderMaximize`, `ElectricBorderTiling` | on | off | an ordinary drag stays a move; QindaQt's Meta+Shift drag docks |
| `[MouseBindings] CommandAll3` (Meta+right button) | Resize | Nothing | the shell's live customization chord receives it |
| `[Effect-blur] BlurStrength`, `NoiseStrength` | 15, 5 | 8, 2 | Theming v2 materials (container-wm ADR-0206) |

## Tools

- `qindaqt/tools/rename-identity [--check]`: applies the string-level
  identity rules; idempotent; re-run after every upstream merge.
- `qindaqt/tools/check-install-collisions <stage> [--manifest FILE]...`: fails
  when a staged file has the same path as one from stock kde-plasma/kwin,
  kwin-x11 or kdecoration (from `qlist -e`, or the saved 6.6.6 manifests in
  `qindaqt/manifests/`), or when a staged path still carries a stock name.
- `qindaqt/tools/smoke-test <stage> <out>`: starts a staged install with the
  virtual backend in an isolated `$HOME` and session bus, and lists the bus
  names, D-Bus answers and files it created, and saves `support-information.txt`.

- `qindaqt/tools/check-coinstall-runtime <stage>`: runs stock KWin and the staged
  fork simultaneously on private buses and HOME directories, proving native
  endpoints and config separation without a system install.

The exported QindaQtKWin CMake package reports
QINDAQT_KWIN_BUILD_ACTIVITIES as false. The fork no longer discovers Plasma
Activities, libplasma, Breeze, or Aurorae. Its QindaQt-owned outline,
notifications, effect frames, output locator, and switcher layouts use QtQuick;
classic Plasma presentation units are omitted from the production build. The
separate Plasma Wayland protocol package remains for protocol generation and
KWayland client code, not libplasma (container-wm ADR-0311).

The night-light plugin consumes QindaQt's authenticated Schedule1 frames.
KNightTime is neither discovered at configure time nor linked or started;
the native service in container-wm owns schedule calculation and Settings1
preferences (container-wm ADR-0313). A production configure must succeed with
`-DCMAKE_DISABLE_FIND_PACKAGE_KNightTime=ON`.

## Helper tests

`-DQINDAQT_KWIN_BUILD_TESTS=ON` builds the tests of qindaqt-kwin's own helpers
without KWin's autotests; run them with
`ctest --test-dir build/qindaqt/helpers/terminate-process/tests`.

## Building

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DKDE_INSTALL_USE_QT_SYS_PATHS=ON -DKDE_INSTALL_LIBDIR=lib64 \
      -DKDE_INSTALL_LIBEXECDIR=/usr/libexec -DBUILD_TESTING=OFF
nice -n12 ionice -c3 cmake --build build -- -j8 -l20
DESTDIR=$PWD/stage cmake --install build
```

In the build tree the program is `build/program/qindaqt-kwin`, because
`build/bin/qindaqt-kwin/` is the plugin namespace directory; run it with
`QT_PLUGIN_PATH=build/bin`.

## QindaQt changes

`git log --grep '^qindaqt:'` lists them.

| Change | Why | ADR |
|---|---|---|
| Decoration cutout passes pointer input through | the corner tab leaves part of the title row transparent | 0277 |
| Tablet proximity continues past the decoration | a pen entering range over a title bar reached no application | 0287 |
| Build only what QindaQt ships | KCMs, KRunner, handbook, catalogs, migrations and the systemd unit would collide with stock KWin | 0289 |
| Own program, library, plugin, data, config and D-Bus names | co-installation with stock KWin and Plasma | 0289 |
| Config and state in QindaQt's folders | QindaQt conventions: the compositor's files sit with QindaQt's own | 0289 |
| QindaQt's defaults compiled in | the fork is QindaQt's compositor, not a renamed KDE copy | 0289 |
| Vendored kdecoration, decorations only from the fork namespace | KWin links kdecoration's private library, whose ABI follows Plasma releases | 0289 |
| Native termination of hung processes of root or another user (`qindaqt/helpers/terminate-process`) | KAuth needed polkit-kde and ksysguard's helper; the root helper pins the process with a pidfd and checks its start time, so a reused PID is never signalled | 0289 |
| Plasma Activities unavailable | the fork fixes `KWIN_BUILD_ACTIVITIES` and the exported consumer capability to false; activity-neutral workspace and output contracts remain | 0311 |

## Native shortcut authority candidate

The bounded PF23 adaptation uses `-DKWIN_NATIVE_SHORTCUTS=ON` to select
QindaQt's registry/store, authenticated owner lifetime, standard compatibility
wire, and additive `org.qindaqt.Shortcuts1` endpoint. The option remains OFF
until the private KF6 client contracts and real compositor scenarios qualify
it. With it ON, KGlobalAccelD is neither discovered nor linked. KF6 GlobalAccel
client wire names remain compatibility contracts, with no daemon dependency.

The input collaborator adapts the existing KGlobalAccelD v6.6.6 modifier-only,
press/repeat/release and rolling four-chord logic, preserving copyright. The
legacy adapter imports default-context kglobalshortcutsrc triples once; saved
native values win, and disconnected owners become inactive without losing their
key reservations. Non-default contexts explicitly remain unsupported here.
Desktop-file actions reuse KService/KIO ApplicationLauncherJob, including the
accepted user command-file contract. Full live release qualification is still pending.

Focused source-only contracts can be configured separately from the compositor:
`cmake -S qindaqt/shortcuts/tests -B build/shortcut-contracts -G Ninja` followed
by `cmake --build build/shortcut-contracts -j4` and
`ctest --test-dir build/shortcut-contracts --output-on-failure`.

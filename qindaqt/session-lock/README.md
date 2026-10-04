# Native session-lock server

The downstream QindaQt fork owns `ext-session-lock-v1`, its explicit surface
role, lock authority and render/input fence. The normative protocol is the
installed Wayland protocols staging XML, not an application-specific imitation.
Consumer architecture is ADR-0294 in container-wm.

`SessionLockManagerV1Interface` owns the global. Protocol resources own their
QObject wrappers. `SessionLockController` belongs to WaylandServer for the
compositor lifetime, on its main thread. It owns the fail-closed state;
SessionLockWindow owns output-sized role presentation. Loss of any protocol
resource removes its graphics and never resets the fence. The current lock
client can be replaced after disconnect, but cannot be displaced while alive.

The compositor launches the fixed `${KDE_INSTALL_FULL_BINDIR}/qindaqt-lock`
with its sole private socketpair connection and a whitelist environment. Only
that exact connection can bind the global or unlock; independently launching
the same executable, including through LD_PRELOAD, grants no authority. The
executable and fixed `org.qindaqt.Lock.desktop` entry must be root-owned and
non-writable, including parent hierarchies. The desktop entry requests the
standard interface through the public permission field and supplies no shell
command override. Native binding has no client app-ID or debug bypass.

`X-QindaQt-KWin-Wayland-Interfaces` is an XDG string list. The launcher and
public service discovery share KDE's desktop-entry list decoding, so the
packaged `ext_session_lock_manager_v1;` grants the exact standard interface.
Generic KService QStringList conversion leaves that semicolon in the value and
would reject the trusted locker into clientless black protection. Stock KDE
keys remain insufficient; root-managed paths, the single exact Exec image and
the owned private connection remain required. The bounded metadata/discovery
regression gate is documented in [Helper tests](../README.md#helper-tests); it
does not qualify physical lock/PAM or installed delivery.

The launcher requires Yama ptrace_scope1–3, and the compositor/native children
disable core dumps and dumpability. execve resets dumpability, so Yama protects
the owned child before its early native protection runs. No kernel setting is
changed. Root/CAP_SYS_PTRACE and trusted launcher ancestors are excluded from
ordinary-client isolation; see consumer ADR-0299.

The compositor does not authenticate PAM. The trusted executable must submit
`unlock_and_destroy` only after successful PAM authentication and account
validation. A failure, cancellation, unavailable authenticator or crash must
leave the protocol lock alive or disconnect into the black fallback. PF7 owns
that client; PF8 owns idle/Lock1/ScreenSaver/session-service adaptation.

`locked` waits for actual OutputFrame presentation on every enabled desktop
physical output, including replicated outputs. Generation-tagged feedback
cannot acknowledge a frame from before acquisition or greeter loss. There is
no timeout that manufactures successful presentation. New outputs remain
fenced. Only explicit current native lock surfaces and the compositor-owned
input method while focused on such a surface can receive locked-session input
or appear; desktop overlay graphics are suppressed.

## Focused private proof

Configure with `BUILD_TESTING=ON` and
`QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION=ON`, build `testNativeSessionLock`, then
run `qindaqt/tools/run-native-lock build/bin/testNativeSessionLock`. The flag
injects authorization only for fixture-selected private socketpair peers. This
build refuses installation. Production builds leave the flag OFF and contain
no authorization injection. The runner supplies disposable HOME/XDG roots,
a private D-Bus session and virtual outputs, and reaps only its process group.
Never run fixtures in the physical session or install their compositor.

PF6's focused CTest runs 23 QtTest checks (including setup/cleanup), with no
skips. It covers denial of ordinary clients despite the debug bypass, delayed
physical presentation and pre-acknowledgement role isolation, greeter crash and
replacement, explicit roles, ten fatal protocol-error cases, ordinary/lock
keyboard-pointer-touch delivery and post-unlock recovery, a real compositor-owned
input-method socket, mirrored presentation, locked hotplug, actual screenshot
and five screencast IPC requests, direct QImage/GL capture blackening, presented
black pixels after crash/hotplug, and effects input grabs.

Screenshot requests reject a locked session before the legacy permission bypass.
Screencast acquisition rejects window/output/region/virtual requests, existing
streams close synchronously on lock acquisition, recording stops, and direct
source renders produce black pixels for both CPU and GL targets. The effects
paint chain is suppressed while locked so cached ordinary-window textures cannot
overpaint fallback. Scene chrome and hardware overlays remain fenced.

A separate production build uses `BUILD_TESTING=OFF`,
`QINDAQT_SESSION_LOCK_TEST_AUTHORIZATION=OFF` and
`CMAKE_DISABLE_FIND_PACKAGE_KScreenLocker=ON`. The compositor, screenshot and
screencast targets build without KScreenLocker; the production library contains
no test authorization entry point. The historical `KWIN_BUILD_SCREENLOCKER`
option remains an always-required native enforcement compatibility switch; OFF
is rejected. Vendored public ScreenSaver XML preserves the later PF8 service
boundary, and the superseded upstream lockscreen test remains source history.

This server proof does not authenticate PAM, exercise an installed native locker,
qualify physical DRM, or prove a real PipeWire consumer pipeline across revocation.
PF7 greeter/PAM, PF8 service integration and physical/nested end-to-end delivery
remain separate gates. No package dependency revision or live rollout is implied.

## Native private-launch slice (PF7)

The backend exports `org.qindaqt.KWin.NativeLock1` at
`/org/qindaqt/KWin/NativeLock`: `RequestLock` admits lock/recovery, `Locked`
reports the native fence, and `Protected` reports current-generation actual
physical presentation even without a locker. Property-change signals carry no
authentication authority. There is no D-Bus unlock or PAM-result method.

The additive native lock receipt calls avoid treating a QtDBus method reply as
compositor identity. RequestLockWithReceipt(nonce) returns no value and sends
lockAdmissionReceipt(nonce, admitted) only to the unique D-Bus caller captured
from the request context. RequestStateWithReceipt(nonce) likewise sends one
stateReceipt(nonce, locked, protected) snapshot to that caller. The producer
accepts only exactly 32 lowercase hexadecimal characters. Admission is not
physical protection. Existing RequestLock, properties and change signals stay
available for compatibility; consumers requiring trusted current state use
the targeted receipt and still join it to their independently admitted compositor
attachment.

The matrix adds root-file/alias, environment and real child trust rows. A harmless
preload constructor actually loads in the independent hostile child, whose global
is denied; the owned child strips injection and reports dumpability0. Even its
parent cannot ptrace that disposable protected child. Actual private D-Bus proves
clientless black fallback becomes Protected only after physical feedback. These
fixtures are compile-time confined to the existing non-installable test build.
The native Qt greeter/PAM process and service integration remain later slices.

## Native Qt interoperability fixture

The consumer builds the non-installed `qindaqt-private-lock-window`, using its
real `qindaqt-session-lock` Qt shell integration. Set the explicit test-build
CMake FILEPATH `QINDAQT_PRIVATE_QT_LOCK_PROBE` to that built executable, then run
the full private matrix. The added `nativeQtSurfaces` rows require actual mapped
roles, native input eligibility and physical framebuffer pixels on both outputs
at normal and 150% scale, incremental hotplug/removal and crash-black protection.
No PAM-success/unlock method is exposed by this fixture. The baseline server
matrix without that external consumer remains a separate 27-check gate; PF7
qualification requires the Qt rows too. Production builds contain no fixture
path or authorization. Qt's initial shell construction commit is suppressed
until configure acknowledgement; server security validation is unchanged.


## Native QtQuick and owned PAM fixture

Set the non-installable test cache FILEPATH `QINDAQT_PRIVATE_QT_LOCK_GREETER`
to the consumer's built `qindaqt-private-lock-greeter`. Its private input pipe
supplies synthetic UI actions only; there is no success/result/unlock command.
The actual native controller consumes a real disposable private-confdir PAM
worker's authenticate + account approval and sends standard unlock, then waits
for server sync before quitting. Production has no fixture input/path methods.

The matrix with both Qt fixtures contains 32 checks. The QtQuick
rows cover wrong-password, cancellation and late response, account denial,
Patrol/Reef public scenes, pure OSK model to QML credential sink, current-worker
unlock, and greeter crash while PAM blocks outside callbacks. Parent death kills
the owned worker within150ms and the server remains locked/protected black.
Fatal Qt warnings stay enabled in the greeter fixture; Reef uses the exact
uninstalled prerequisite module that removes its Qt6.11 palette-name collision.

Scale qualification sets OutputChangeSet.scaleSetting (the user preference),
not only the derived effective scale. Both external Qt rows assert actual
backend scale; the QtQuick fixture also proves Qt DPR1.50. The earlier named
150% row's derived-scale-only assignment did not prove physical scale and is
superseded. The correct full CTest selector is
`^kwin-testNativeSessionLock$`, always with `--no-tests=error`.

Installed root-owned binding, physical DRM, real owner password/service-stack
and coherent Portage delivery remain held qualification boundaries. The optional non-installable cache FILEPATH `QINDAQT_PRIVATE_GATHERED_PLUGIN`
loads one exact consumer plugin through its public PluginFactory and adds the
33rd matrix check. The private row creates a real Hybrid group with Meta+Shift
drag, iconifies another client with Meta+wheel, and selects roll-up through the
public QWidget popup UI (the compositor test QPA is offscreen). It proves actual
chip and shaded-strip pixels before lock, then actual rendered hover caption
text beside the chip. The owned Qt greeter covers all three regions; screenshot
requests reject, and CPU/GL Window/Output/Region capture
sources return black. Actual greeter crash leaves the whole output black; a
replacement owned greeter's native PAM/OSK unlock restores both gathered ink
regions and the real hover caption. The native cursor is hidden only for uniform
coverage/black pixel probes; it remains allowed by production lock policy.
Cached geometry inventory is not treated as visibility evidence.
Use the exact consumer artifact and rerun after consumer integration; never
substitute a stale development plugin root or grant a production test bypass.

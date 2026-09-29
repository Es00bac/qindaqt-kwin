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

The fixed `${KDE_INSTALL_FULL_BINDIR}/qindaqt-lock` executable must be regular,
root-owned and not group/world writable, through a root-owned non-writable
parent hierarchy. Peer UID must equal compositor UID, peer `/proc/PID/exe`
inode/device must match, and a desktop entry for that executable must request
`ext_session_lock_manager_v1` through the public Wayland interface permission
mechanism. Sandboxed app IDs, same-process clients and the general debug
permission bypass cannot grant this global. Unlock rechecks the peer identity.

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

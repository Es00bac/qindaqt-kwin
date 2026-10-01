# CaptureAuthority wire version 1 — proposed integration freeze

This is the public compositor/portal process boundary for protected native
Screenshot and ScreenCast. The constants in `protocol.h` are installed under
`/usr/include/qindaqt-kwin/capture-authority/protocol.h`; the separately built
consumer includes that installed header, not a fork-private source header.
The compositor owns process admission and revocation. The capture-only portal
broker owns frontend Request/Session/app policy and publication; its native
helper owns selection, parent import and consent. The existing general portal
resident stays supervisor-owned under ADR-0318. This wire does not add a
public D-Bus grant method and grants no privilege from an app ID, path, PID or
UID supplied by an ordinary process.

## Fixed launch and descriptors

The compositor may launch only `/usr/libexec/qindaqt-portal-capture-backend`
as broker and `/usr/libexec/qindaqt-portal-capture` as per-job helper. Their
desktop entries are respectively
`/usr/share/applications/org.qindaqt.PortalCaptureBackend.desktop` and
`/usr/share/applications/org.qindaqt.PortalCapture.desktop`. On each launch,
canonical executable and desktop files and every original/canonical parent
must be root-owned and non-group/world-writable, and desktop `Exec` must be
exactly the compiled-in executable with no extra arguments. The helper entry
declares only `org.kde.KWin.ScreenShot2` for its legacy compatibility key;
the broker entry declares no general restricted permission. The compositor
requires Yama `ptrace_scope` 1–3, sets core limits to zero, uses fixed empty
argv and a sanitized allowlist environment, resets IDs and signal handlers,
closes other descriptors and works in `/`. No `LD_*`, `QT_PLUGIN_PATH`,
QML import override, public `WAYLAND_DISPLAY`, inherited capture-bypass
variable or shell command is forwarded. The child applies
`PR_SET_DUMPABLE=0` and verifies it before Qt, D-Bus or Wayland startup.

Both broker and helper inherit **fd 3**, a compositor-created
`AF_UNIX/SOCK_SEQPACKET|SOCK_CLOEXEC` private control socketpair endpoint.
The compositor clears `FD_CLOEXEC` only on the intentional child endpoint
after duplicating it to fd 3; it retains the other endpoint. The helper also
inherits fd 4 (ordinary compositor-created Qt Wayland connection), fd 5
(separate tagged restricted capture Wayland connection), fd 6 (read end of a
broker-to-helper bounded request pipe) and fd 7 (write end of a
helper-to-broker bounded result pipe). The compositor transfers the other two
pipe ends to the broker in `JobStarted` via `SCM_RIGHTS` and closes its own
copies. The helper never obtains the broker control socket, and the broker
never obtains the helper control or capture socket. Ordinary GUI/foreign-parent
globals live only on fd 4. fd 5 is tagged by exact `ClientConnection *`
before the self-PID global shortcut and sees only `wl_output`,
`zxdg_output_manager_v1` and `zkde_screencast_unstable_v1`.

The launcher uses one broker per selected current `org.qindaqt.KWin` D-Bus
owner. The broker claims only
`org.freedesktop.impl.portal.desktop.qindaqt.capture` and must not have a
standalone D-Bus activation command. The general portal backend and its
Session1 attachment are untouched. A missing/preclaimed broker name makes
capture unavailable; it never admits the preclaiming process. A bounded
restart uses a fresh broker generation and retires all old jobs.

## Framing and authenticated handshake

One `sendmsg` corresponds to one `recvmsg` packet. Each packet begins with a
32-byte little-endian header: offsets 0..3 magic (`QCC1`), 4..5 version,
6..7 `Message`, 8..15 compositor generation, 16..23 job ID (zero for broker
handshake), 24..27 payload length, 28..29 descriptor count, 30..31 reserved
zero. Payload is at most 16 KiB; descriptor count is zero except exactly two
on `JobStarted`. Reject truncated packets, unknown types/version, unexpected
ancillary messages, wrong FD count, nonzero reserved bits, duplicated job IDs,
generation mismatch and out-of-order transitions, closing all received FDs.
No unbounded queue is permitted; at most 16 jobs exist per broker.

Each receiver enables `SO_PASSCRED` and requires `SCM_CREDENTIALS` on every
packet. The compositor compares kernel UID/PID on *post-exec* child messages
with its fixed-binary QProcess launch and still-live pidfd, and requires the
private channel to be the exact retained endpoint. The broker and helper
compare compositor packet credentials with the inherited compositor peer,
its still-live pidfd and the **current** native D-Bus owner/PID; `Hello`'s
generation alone is not proof. `SO_PEERCRED` on a socketpair made before
fork/exec can describe its compositor creator and is insufficient to
authenticate a child after exec.
Opening a pidfd after launch is not by itself identity: the private channel,
post-exec credentials, exact launch PID and (for helper Screenshot2) D-Bus
unique sender must still join. A malformed or lost channel is terminal.

The compositor sends `Hello` with a nonzero generation to each new child.
After PR0 and, for broker, successful capture backend bus-name registration,
the child echoes `Ready` with its own unique D-Bus sender in a length-prefixed
UTF-8 payload. The compositor asks the session bus for that unique sender's
UID/PID and checks current ownership of the broker name. The helper's `Ready`
similarly binds its new unique sender, but has no well-known service name.
All sender names are D-Bus unique names; a self-reported name cannot replace
bus-daemon UID/PID and retained pidfd. The broker additionally confirms the
current compositor unique owner/PID before handling frontend calls.

The v1 payload table is exact; a receiver rejects trailing bytes as well as
missing fields. `Hello`, `JobStarted`, `ParentReady`, `ConsentGranted`,
`CaptureReady`, `RevokeJob` and `JobRevoked` have empty payloads. `Ready` is
little-endian `uint16 nameBytes` followed by exactly that many UTF-8 bytes
of a D-Bus unique name (`1..255`, no NUL); the bus daemon, never this field,
supplies its UID/PID. `StartJob` and `Error` use the layouts below. Broker
handshake packets use job ID zero; helper handshake and every job packet use
their assigned nonzero job ID. The first `Hello` fixes the generation for
that child, and no later packet may change it.

| Message | Sender to receiver | Job ID | Payload | FDs |
| --- | --- | --- | --- | --- |
| `Hello` | compositor to broker or helper | zero for broker; assigned ID for helper | empty | zero |
| `Ready` | broker or helper to compositor | matches `Hello` | `uint16 nameBytes`, then exact unique sender bytes | zero |
| `StartJob` | broker to compositor | broker-allocated ID | `Scope` plus two exact unique names below | zero |
| `JobStarted` | compositor to broker | echoes `StartJob` | empty | two, in the stated order |
| `ParentReady`, `ConsentGranted` | helper to compositor | assigned ID | empty | zero |
| `CaptureReady` | compositor to helper | assigned ID | empty | zero |
| `RevokeJob` | broker or helper to compositor | assigned ID | empty | zero |
| `JobRevoked` | compositor to broker and helper while each channel exists | assigned ID | empty | zero |
| `Error` | compositor to the relevant child | assigned ID, or zero for broker handshake failure | `uint16 Error` | zero |

The trusted broker allocates a strictly increasing, nonzero `uint64` job ID
within its compositor generation, after admitting the portal request and
before sending `StartJob`. The compositor remembers the highest submitted ID
for that generation and rejects zero, reuse, wrap or non-increasing values;
it echoes the accepted ID in `JobStarted` and the helper's `Hello`. No caller
field, PID or app ID chooses an ID, and IDs are correlation keys rather than
bearer authorization. The authenticated private channel plus current state
always decide authority.

## Job state and payloads

`StartJob` is broker-to-compositor after the real portal frontend call and
request/session admission. Payload: little-endian `Scope` (2 bytes), frontend
unique-name length (2), caller unique-name length (2), reserved zero (2),
then those exact UTF-8 names. Each name is 1..255 bytes; no trailing NUL or
app ID is carried. Compositor independently queries the D-Bus daemon for both
current names' UID/PID and retains pidfds. It requires the broker channel and
its selected compositor generation, known/unlocked native privacy and a free
job slot; otherwise it sends terminal `Error`. It does not parse portal request/session handles or user
options. The broker binds each job ID to its validated request/session/parent
and sends `RevokeJob` on Close, actor/parent loss or publication failure.
`Scope::Screenshot` covers the capture backend's Screenshot and PickColor
portal intents; the only privileged KWin method it may admit is ScreenShot2.
There is no implied ColorPicker/global or unrelated restricted permission.
`Scope::ScreenCast` allows only the restricted screencast global and stream
operations. A job cannot change scope or mint another interface.

On success the compositor creates a fixed helper launch plus separate GUI and
capture socketpairs and request/result pipes. `JobStarted` transfers exactly
two FDs to the broker in order: request writer, result reader. The broker
sends its bounded native request over that pipe to its launched helper; no
compositor policy derives from the frame. The helper reports `ParentReady`
only after valid nonempty foreign-parent import, or after explicitly accepting
an empty parent. It reports `ConsentGranted` only after the user's native
Allow/Share action. The compositor grants Screenshot2 to that helper's exact
D-Bus unique sender or allows capture-stream creation on that job's exact
Wayland client only after both messages and current known/unlocked privacy.
It sends `CaptureReady` for that job/scope; the helper waits for this before
issuing Screenshot2 or `stream_output`. Manager visibility before consent is
enumeration only; no pixels or stream may be created. The broker receives the
helper result through its pipe and independently rechecks frontend/caller,
session, parent, compositor and native privacy before publishing URI/node.

For a Screenshot-scope job, the compositor permits **one** ScreenShot2 method
invocation after `CaptureReady`. It marks the grant consumed at method entry;
that operation's revocable lease may complete, but every later ScreenShot2
invocation from the same helper/job is denied, including while the helper is
still alive. The helper sends at most one result, hides its UI and remains
idle with its control channel open while the broker retains the private
screenshot file/URI for at most 300,000 ms **after the result**. This retained
state grants no second capture. The broker owns the file and rechecks actor,
parent, current native privacy, helper and compositor lineage on every
read-through or publication. It sends `RevokeJob` at retention expiry, Close
or any lost lineage; the compositor also revokes directly on its listed
failure events. The helper then exits. PickColor uses the same one-shot
Screenshot2 grant, but its single color result must finish publication before
the broker intentionally revokes the job; it has no retained image URI. If
the helper exits early, HUP revokes the job and the broker withdraws its
retained URI even if the file still exists. The 30-second protected write
deadline does not shorten this bounded retention lifetime.

`RevokeJob`, `JobRevoked` and `Error` are terminal and idempotent for the same
job. `Error` payload is a two-byte little-endian `Error` code only; text or
private record content is never sent. The compositor directly revokes on
lock/unknown privacy, selected owner/generation change, broker/helper
channel HUP or process exit, helper D-Bus sender replacement, frontend/caller
owner or pidfd loss and pre-consent deadline. Helper `ParentReady` cannot
survive its subsequent parent-loss `RevokeJob`. Revoke destroys the exact
tagged capture Wayland client, closes its streams, cancels all job Screenshot2
operations and establishes the async writer no-more-writes barrier. A
terminal job cannot be reactivated or transferred. An ordinary Wayland
reconnect is never tagged; a fresh job needs new frontend request/consent.

## Capture and deadline compatibility

All seven public ScreenShot2 methods, including `CaptureInteractive`, require
the existing ordinary restricted desktop permission or a live protected exact
job/sender lease, plus known/unlocked native privacy. Legacy authorized
interactive selection remains allowed. Protected method entry, delayed
selection, sink, reply and writer check the same lease; legacy and protected
operations each send at most one terminal reply. The existing
`org.kde.KWin`/`org.kde.KWin.ScreenShot2` wire names and `X-KDE-*` carve-outs
remain until PF21.

For a protected Screenshot2 job, the public port's earlier outer timer is
authoritative: 30,000 ms from call send to complete pipe EOF and terminal
D-Bus reply, with its pending-call timeout also 30,000 ms. The fork has
28,000 ms from later method invocation to finish write, close pipe and send
success, leaving an explicit 2,000 ms nominal transport margin. The fork
never resets or extends the portal's earlier deadline; delayed bus delivery
can conservatively fail. Both sides must cancel and discard late bytes/replies
on expiry or revoke. A continuously drained 16–20-second protected write
must complete and receive one success reply. Ordinary desktop-entry clients
retain existing 15,000-ms D-Bus plus 10,000-ms pipe grace and interactive
selection timing. The helper's 60-second pre-consent expiry retires when
Allow begins protected capture. Bytes or a reply delivered before revocation
cannot be recalled; no new bytes, reply or URI may publish after it.

The 30,000-ms outer and 28,000-ms fork clocks apply only to each protected
Screenshot2 invocation, starting when that invocation is sent/received as
specified above. They do **not** expire a ScreenCast stream or its session.
For ScreenCast, the 60-second pre-consent deadline bounds helper selection;
after Share and `CaptureReady`, its stream and frame authority continue only
while the exact request/session, parent, helper, broker, actors, compositor
generation and known/unlocked privacy remain live. Any revocation closes the
stream and stops new frames; a later session requires a new job and consent.

The only test authority hook is compiled under an explicit non-installable
test flag and is absent from production builds. It cannot use an environment
variable to bypass permission checks or relax PR0.

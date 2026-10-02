# Conditional source acceptance: unified protected broker and EIS owner

- Exact desktop candidate `30df741d07377343a31a3851b9b01e0ba5368fe7`, isolated `/home/cabewse/.cache/pf-unified-broker-review-20261002`, branch `worker/pf-unified-broker-review-20261002`.
- Exact fork candidate `1321e0e39769890ddc6c967b319af5dede8ce5fb`, isolated `/home/cabewse/.cache/pf-eis-owner-review-20261002`, branch `worker/pf-eis-owner-review-20261002`.
- Independent reviewer role; no source mutation or compiler/native/host execution. Both exact `git diff HEAD^ HEAD --check` exit0. Runtime tests run:0. This is conditional SOURCE ACCEPT, not release/runtime qualification.

## Findings

1. Fork `src/plugins/eis/eisportaladmission.cpp:15–37` changes only the named current owner. D-Bus origin, unique sender, same effective UID remain required. Its consumers cover RemoteDesktop EIS, Clipboard and InputCapture manager. Existing creator-sender cookies, owner-disconnect cleanup, and native lock clearing are unchanged. Ordinary resident and stock KDE owners no longer match this admission.
2. Desktop `capture/backend/main.cpp:30–65` retains inherited ControlFd/native initialized/QCC1 start checks. New ordinary binding and fixed configured consent path compose existing RemoteDesktop/InputCapture/Clipboard with Screenshot/ScreenCast; combined ScreenCast borrows the same remote source delegate and AuthorityCapture. Stack declaration/destruction order destroys all adaptors before the borrowed ports and remote before capture storage. No unrelated service enters protected broker.
3. AuthorityCapture Git blob `0832c77d2d048fd4e4956c37777ebafedf03ab4c`, channel `324e9ced39f75130fa8adee8ad1c61d568a0dcf0`, native admission `42263c43ca4e5eda23bcf1984edd2878ff34f45f` each match the parent exactly. Existing actual frontend/current caller identity, caller PIDFD, per-job retirement and capture scope remain unchanged; no fabricated actor or input capture capability is introduced.
4. New distinct PortalCapture1 service/path retains existing Portal1 wire interface and AttachSessionWithDisplay contract. First same-UID unique session caller cannot be replaced; CompositorAttachment retains selected native owner/PID, public peer UID/PID, compositor PIDFD/socket lifetime, current caller registration and ordinary FD handoff. This attachment grants no capture capability.
5. Supervisor `portal_session_lifetime.cpp:27–70` watches the distinct name, sends to exact unique owner with AutoStartService=false, and calls child.start only for nonempty program. attachCapture passes no program. Its independent bus is stopped in both supervisor stop/finish paths. Generations reject stale replies. Existing consent binding/lock loss clears input sessions; new broker handler retires registry/capture and exits; capture loss clears remote and screen sessions and exits. Existing producer/session Close links retain combined cleanup.
6. ADR0341 is Proposed and truthfully requires coordinated native/frontend qualification; new attachment source test is unexecuted. No source blocker found in this bounded review.

## Required assembly conditions

- Root must integrate matching fork1321, exact broker30df or reviewed successor, and route all five capture/input families together to qindaqt.capture, defaultnone. Exact30df still creates resident capture/input adaptors in foundation_composition.cpp; root's forthcoming composition must produce the promised ACTUAL ordinary12/protected5 exports, not just change metadata advertisements.
- EIS predicate proves current same-UID named owner; it does not independently prove inherited-capability lineage. A service-name spelling is not security evidence. Fixed compositor launch/current broker receipt and protected startup remain separate reviewed contracts, and coherent live gates must qualify them.
- Actual private/native/frontend tests must cover combined streams plus EIS/Notify/Clipboard, InputCapture, standalone remember/restore, Close/frontend/caller owner loss, native lock and ordinary binding loss. No prior test PASS is relabeled as exact30df runtime acceptance.

Requested next action: manager integrates the coordinated source after review conditions, then owns actual bounded combined qualification; blocking runtime reproduction goes to the original implementer. Signed Gabbee artifact receipt remains overlay10c9d74.

# reTerminal E1001/E1002 hardware events

E1001 and E1002 expose hardware changes through the existing command/result transport.
No new unsolicited `link.*` wire messages are assumed. A cloud program must
keep an `events.wait` request outstanding and process each response to run
actions automatically. Advertising this command alone does not start a Muse
automation or guarantee that the cloud runs a listener.

- `button.changed`: `key` (`key0`, `key1`, `key2`), `gpio`, `pressed`, `presses`.
  Both press and release are debounced and recorded. GPIO3 is KEY0, GPIO4 is
  KEY1, GPIO5 is KEY2. KEY0 still has the firmware's pairing/reset gestures.
- `sd.changed`: `gpio` (15), `inserted`. An initial card-detect snapshot is
  recorded at boot, followed by debounced changes. This reports the socket
  detection pin, not whether FAT mounting or file operations succeeded.
- Every record includes `sequence` and `uptime_ms`, relative to this boot.

All successful data replies use `{"ok":true,"payload":{...}}`, matching
the SDK's `link.result` contract. `events`, `next_sequence`, `has_more`,
`history_lost` and `cursor_reset` are inside `payload`. The same convention
applies to button state, RTC and SD queries. Acknowledgements without data
may contain only `ok`.

`buttons.read` remains available for current state and cumulative press counts.
Temperature, humidity, battery and RTC remain explicit reads; no thresholds,
scheduled actions, page lists or button actions are installed by these APIs.

## Listening

1. Call `events.read` without parameters to get the current `next_sequence`.
   Use `{"after":0}` instead to read retained records from this boot.
2. Call `events.wait` with `{"after":123,"timeout_ms":25000}`, using the cursor
   just received. Pending waits complete through normal asynchronous command
   results. Other commands can run while a wait is pending.
3. Process `events`, then repeat with `next_sequence`, even after an empty
   timeout. For a press action, use only `button.changed` with `pressed=true`.
4. If `has_more` is true, drain the next batch immediately with the same API.

For a hardware check, serial logs print `e100x.buttons: KEY1 GPIO4 pressed`
and the matching release after debouncing. Then read retained history with
`events.read {"after":0}` and check `payload.events`.

History keeps the latest 32 events in RAM and returns at most 16 per response.
Reads do not consume records. Slow consumers receive `history_lost=true` if
older events were overwritten. Delivery is not durable across power loss.
Reset the cursor to 0 after reboot. A cursor ahead of the current history
returns `cursor_reset=true` and replays retained events. After reconnecting to
the same boot, reuse the cursor to recover changes within retained history.

One `events.wait` may be pending; another gets `busy`. Timeout is 1–30000 ms,
default 25000 ms; the advertised command deadline is 35000 ms. Offline waits
end early when detected, or at timeout. Results are bound to their original
session, so they cannot be delivered as replies on a different session.

For example, a cloud listener can respond to a KEY1 press by calling
`display.draw_url`. This requires connectivity and cloud execution. Local
image caching, offline actions and timers are separate capabilities.

# Robot layout and movement

The robot sits in a chair beside a desk. A miniature laptop and a service bell sit on the desk in front of it. Five micro servos drive the pose; limits and channel IDs are in [`include/servos.h`](../include/servos.h).

| Index | Servo | Motion | At `min` | At `max` | Safe range (°) |
|------:|-------|--------|----------|----------|----------------|
| 0 | **Head** | Face up / down (pitch) | down | up | 80 – 100 (stock) |
| 1 | **Neck** | Head left / right (yaw) | right | left | 80 – 100 (stock) |
| 2 | **Left hand** | Hand up / down | up | down | 80 – 100 (stock) |
| 3 | **Right hand** | Hand up / down (inverted scale) | down | up | 80 – 100 (stock) |
| 4 | **Body** | Whole torso left / right | right | left | 80 – 100 (stock) |

Stock defaults match `SERVO_SPECS` in [`include/servos.h`](../include/servos.h): **80°–100°** on every joint when NVS is empty (mid 90°). After setup AP guided calibration the wider per-build limits are stored in NVS. Factory reset keeps the saved ranges. Retune them from the setup wizard servos hub (**Calibrate servos**) when AP mode is open, or **Skip** to keep the current values.

Animations author poses in **−1..1** relative to each joint’s saved min/max (`−1` = min, `0` = mid, `+1` = max). Horn offset after assembly is absorbed by calibration; firmware maps those poses with `servoNormToDeg`. Right-hand rest is still electrical min (`−1`); left-hand rest is electrical max (`+1`).

**Head** tilts the face toward or away from the laptop. **Neck** pans the head side to side. **Hands** lift and lower over the keyboard; the forearms have no elbow servo — the arm linkage is fixed, so only the hand joint moves. **Left** and **right** hand servos use opposite scales: on the left, higher angle is down; on the right, higher angle is up (rest pose for typing is left at `max`, right at `min` — both over the keyboard). Setup AP guided calibration sets those keyboard bounds first, then the upper bounds. **Body** rotates the whole upper body in the chair while the base stays put.

Command a single joint for bench checks:

```bash
curl -X POST "http://tiny-engineer.local/test/servo" -H "Content-Type: application/json" -d '{"index":0,"angle":90}'
```

Use angles inside the saved safe range on the assembled robot. During setup AP, `POST /setup/servo` can command the full 0–180° electrical range so you can find those limits.

## Voice cue moves

A streamed clip can carry `move` cues ([api.md](api.md#voice-cues)). They use the same −1..1 units, so `hand_right=1` is the right hand up and `hand_left=-1` is the left hand up (opposite scales, as above). `neck=1` turns the head to the robot's left, `body=1` turns the torso to its left, `head=1` is face up. The cue holds only the joints it names: the running animation keeps moving all other joints. When the hold ends, the joint goes back to the last target the animation asked for, at the animation's own speed. A very short `ease` is capped at the servo top speed, so the joint never jumps. Park and a servo driver fault end every hold at once.

Related: [servos.md](hardware/servos.md) (PWM and electrical limits), [api.md](api.md) (`/test/servo`, `/anim`).

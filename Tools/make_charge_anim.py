# Builds /PogoStick/Animations/AS_PogoStick_Charge on the Pioneer's first-person skeleton: both
# hands on the Pogo Stick's handlebar, with the stick pushed in from frame 0 (held) to the last
# frame (fully charged). The game freezes it at the frame matching the charge, so it's posed per
# frame, not timed. Starts from the Rebar Gun idle; the arms are moved with an analytic two-bone IK
# (shoulder fixed, elbow keeps its bend) and the game's hand IK bones follow, so the anim graph's
# hand IK lands on the same spot. The left hand is the mirror image of the right. Re-runnable.
# The SML project's 1P arm *mesh* is a stand-in, but its skeleton and animations hold real bone
# data, which is all this needs.
#   UnrealEditor-Cmd.exe C:\SF\SML\FactoryGame.uproject -run=pythonscript
#     -script=C:\SF\PogoStick\Tools\make_charge_anim.py -unattended -nosplash
import math
import unreal

SOURCE = "/Game/FactoryGame/Character/Player/Animation/FirstPerson/RebargunIdle_01"
DEST_DIR = "/PogoStick/Animations"
DEST_NAME = "AS_PogoStick_Charge"

# Where the game holds the stick in first person, in the ArmBase bone's space (the camera pivot:
# +X left, +Y forward, +Z up, cm). Must match FirstPersonHandlebarCentre and FirstPersonChargePush
# in PogoStickEquipment.cpp, and the default Size setting.
HANDLEBAR_CENTRE = (0.0, 38.0, -12.0)
CHARGE_PUSH = (0.0, 6.0, -5.0)
HOLD_SCALE = 0.7
GRIP_HALF_WIDTH = 15.75 * HOLD_SCALE   # PogoStickAssets::RightGripOffset
FRAMES = 30          # keys 0..FRAMES: frame / FRAMES is how far the stick is pushed in
FRAME_RATE = 30

eal = unreal.EditorAssetLibrary
APE = unreal.AnimPoseExtensions


def log(message):
    unreal.log(f"[PogoCharge] {message}")


# --- Small vector and quaternion helpers (x, y, z) / (x, y, z, w), UE conventions -------------
def v(t):
    return (t.x, t.y, t.z)


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def length(a):
    return math.sqrt(dot(a, a))


def norm(a):
    l = length(a)
    return mul(a, 1.0 / l) if l > 1e-6 else (0.0, 0.0, 0.0)


def q(uq):
    return (uq.x, uq.y, uq.z, uq.w)


def qmul(a, b):
    # a * b: apply b, then a (UE FQuat convention).
    ax, ay, az, aw = a
    bx, by, bz, bw = b
    return (aw * bx + ax * bw + ay * bz - az * by,
            aw * by - ax * bz + ay * bw + az * bx,
            aw * bz + ax * by - ay * bx + az * bw,
            aw * bw - ax * bx - ay * by - az * bz)


def qinv(a):
    return (-a[0], -a[1], -a[2], a[3])


def qbetween(a, b):
    a, b = norm(a), norm(b)
    c = cross(a, b)
    w = 1.0 + dot(a, b)
    if w < 1e-6:
        return (0.0, 0.0, 1.0, 0.0)
    l = math.sqrt(c[0] ** 2 + c[1] ** 2 + c[2] ** 2 + w ** 2)
    return (c[0] / l, c[1] / l, c[2] / l, w / l)


def qrot(a, vec):
    p = qmul(qmul(a, (vec[0], vec[1], vec[2], 0.0)), qinv(a))
    return (p[0], p[1], p[2])


def to_uquat(a):
    return unreal.Quat(a[0], a[1], a[2], a[3])


# 3x3 matrices as row tuples; a rotation matrix's columns are the rotated X, Y, Z axes.
def q_to_m(a):
    x, y, z, w = a
    return ((1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
            (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
            (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)))


def m_to_q(m):
    t = m[0][0] + m[1][1] + m[2][2]
    if t > 0:
        s = math.sqrt(t + 1.0) * 2
        r = ((m[2][1] - m[1][2]) / s, (m[0][2] - m[2][0]) / s, (m[1][0] - m[0][1]) / s, 0.25 * s)
    elif m[0][0] > m[1][1] and m[0][0] > m[2][2]:
        s = math.sqrt(1.0 + m[0][0] - m[1][1] - m[2][2]) * 2
        r = (0.25 * s, (m[0][1] + m[1][0]) / s, (m[0][2] + m[2][0]) / s, (m[2][1] - m[1][2]) / s)
    elif m[1][1] > m[2][2]:
        s = math.sqrt(1.0 + m[1][1] - m[0][0] - m[2][2]) * 2
        r = ((m[0][1] + m[1][0]) / s, 0.25 * s, (m[1][2] + m[2][1]) / s, (m[0][2] - m[2][0]) / s)
    else:
        s = math.sqrt(1.0 + m[2][2] - m[0][0] - m[1][1]) * 2
        r = ((m[0][2] + m[2][0]) / s, (m[1][2] + m[2][1]) / s, 0.25 * s, (m[1][0] - m[0][1]) / s)
    n = math.sqrt(sum(x * x for x in r))
    return tuple(x / n for x in r)


def mmul(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def mtrans(a):
    return tuple(tuple(a[j][i] for j in range(3)) for i in range(3))


def reflection(n):
    # Mirror across the plane with unit normal n.
    return tuple(tuple((1.0 if i == j else 0.0) - 2 * n[i] * n[j] for j in range(3)) for i in range(3))


MIRROR_X = reflection((1.0, 0.0, 0.0))


# --- IK ----------------------------------------------------------------------------------------
def solve_arm(world, side, offset, target=None, hand_rotation=None):
    """New world rotations for upperarm, lowerarm and hand that put the hand at target (or its
    idle position plus offset), keeping the elbow's bend direction. The hand keeps its world
    orientation unless hand_rotation is given."""
    up, lo, ha = f"upperarm_{side}", f"lowerarm_{side}", f"hand_{side}"
    S, E, H = v(world[up].translation), v(world[lo].translation), v(world[ha].translation)
    T = target if target is not None else add(H, offset)
    a, b = length(sub(E, S)), length(sub(H, E))
    d = min(length(sub(T, S)), a + b - 0.5)
    u = norm(sub(T, S))
    # The original elbow's offset from the shoulder-hand line sets which way the elbow bends.
    p = sub(E, S)
    axis0 = norm(sub(H, S))
    p = norm(sub(p, mul(axis0, dot(p, axis0))))
    p = norm(sub(p, mul(u, dot(p, u))))
    x = (a * a - b * b + d * d) / (2.0 * d)
    h = math.sqrt(max(a * a - x * x, 0.0))
    E2 = add(add(S, mul(u, x)), mul(p, h))
    T2 = add(S, mul(u, d))

    rU, rL, rH = q(world[up].rotation), q(world[lo].rotation), q(world[ha].rotation)
    if hand_rotation is not None:
        rH = hand_rotation
    if length(sub(T, T2)) > 0.5:
        log(f"{ha} can't reach: {length(sub(T, T2)):.1f} cm short")
    dU = qbetween(sub(E, S), sub(E2, S))
    newU = qmul(dU, rU)
    rL1 = qmul(dU, rL)
    dL = qbetween(qrot(dU, sub(H, E)), sub(T2, E2))
    newL = qmul(dL, rL1)
    return {up: newU, lo: newL, ha: rH}, {up: S, lo: E2, ha: T2}


# --- Build -------------------------------------------------------------------------------------
source = unreal.load_asset(SOURCE)
dest_path = f"{DEST_DIR}/{DEST_NAME}"
if eal.does_asset_exist(dest_path) or unreal.load_asset(dest_path) is not None:
    eal.delete_asset(dest_path)
anim = eal.duplicate_asset(SOURCE, dest_path)
log(f"duplicated to {anim}")

controller = anim.controller
log(f"controller={controller}")
controller.open_bracket(unreal.Text("Pogo charge"), False)
controller.set_frame_rate(unreal.FrameRate(FRAME_RATE, 1), False)
controller.set_number_of_frames(unreal.FrameNumber(FRAMES), False)

bone_names = [str(n) for n in unreal.AnimationLibrary.get_animation_track_names(source)]

# A left bone's axes are the mirror of the right one's plus a fixed local correction, found from
# the reference pose, where the rig is symmetric across component X.
ref = APE.get_reference_pose(source.get_skeleton())
ref_r = q_to_m(q(APE.get_bone_pose(ref, "hand_r", unreal.AnimPoseSpaces.WORLD).rotation))
ref_l = q_to_m(q(APE.get_bone_pose(ref, "hand_l", unreal.AnimPoseSpaces.WORLD).rotation))
LEFT_CORRECTION = mmul(mtrans(mmul(mmul(MIRROR_X, ref_r), MIRROR_X)), ref_l)
# The Rebar Gun idle's left fingers cup the barrel from below and dig into the handlebar, so the
# left fingers copy the right hand's grip, mirrored, with each bone's own correction.
FINGER_CHAINS = [[f"{finger}_0{n}" for n in (1, 2, 3)] for finger in ("thumb", "index", "middle", "ring", "pinky")]
FINGER_CORRECTION = {}
for chain in FINGER_CHAINS:
    for bone in chain:
        r = q_to_m(q(APE.get_bone_pose(ref, f"{bone}_r", unreal.AnimPoseSpaces.WORLD).rotation))
        l = q_to_m(q(APE.get_bone_pose(ref, f"{bone}_l", unreal.AnimPoseSpaces.WORLD).rotation))
        FINGER_CORRECTION[bone] = mmul(mtrans(mmul(mmul(MIRROR_X, r), MIRROR_X)), l)
log(f"reference hand_r {v(APE.get_bone_pose(ref, 'hand_r', unreal.AnimPoseSpaces.WORLD).translation)} "
    f"hand_l {v(APE.get_bone_pose(ref, 'hand_l', unreal.AnimPoseSpaces.WORLD).translation)}")
options = unreal.AnimPoseEvaluationOptions()
keys = {name: ([], [], []) for name in bone_names}

for i in range(FRAMES + 1):
    push = i / FRAMES
    pose = APE.get_anim_pose_at_time(source, 0.0, options)
    names = [str(n) for n in APE.get_bone_names(pose)]
    world = {n: APE.get_bone_pose(pose, n, unreal.AnimPoseSpaces.WORLD) for n in names}
    local = {n: APE.get_bone_pose(pose, n, unreal.AnimPoseSpaces.LOCAL) for n in names}

    base_rot, base_pos = q(world["ArmBase"].rotation), v(world["ArmBase"].translation)
    centre = add(base_pos, qrot(base_rot, add(HANDLEBAR_CENTRE, mul(CHARGE_PUSH, push))))
    bar = norm(qrot(base_rot, (1.0, 0.0, 0.0)))   # towards the left grip
    grip_r = add(centre, mul(bar, -GRIP_HALF_WIDTH))

    # Right hand: the grip point (hand_rSocket, where the Rebar Gun's handle sits) on the grip. The
    # Rebar Gun's handle runs along the socket's Z, thumb end up; turn that onto the handlebar,
    # thumb inwards, so the palm comes over the top.
    socket_pos, socket_rot = v(local["hand_rSocket"].translation), q(local["hand_rSocket"].rotation)
    idle_rot = q(world["hand_r"].rotation)
    handle_axis = qrot(qmul(idle_rot, socket_rot), (0.0, 0.0, 1.0))
    hand_r_rot = qmul(qbetween(handle_axis, bar), idle_rot)
    hand_r_pos = sub(grip_r, qrot(hand_r_rot, socket_pos))

    # Left hand: the mirror image across the plane through the handlebar's centre.
    mirror = reflection(bar)
    rel = sub(hand_r_pos, centre)
    hand_l_pos = add(centre, sub(rel, mul(bar, 2 * dot(rel, bar))))
    hand_l_rot = m_to_q(mmul(mmul(mmul(mirror, q_to_m(hand_r_rot)), MIRROR_X), LEFT_CORRECTION))

    new_world_rot = {}
    new_world_pos = {}
    for side, target, rotation in (("r", hand_r_pos, hand_r_rot), ("l", hand_l_pos, hand_l_rot)):
        rots, poss = solve_arm(world, side, None, target, rotation)
        new_world_rot.update(rots)
        new_world_pos.update(poss)

    # Parents: upperarm under clavicle (unchanged), lowerarm under upperarm, hand under lowerarm.
    new_local_rot = {}
    for side in ("r", "l"):
        clav = q(world[f"clavicle_{side}"].rotation)
        new_local_rot[f"upperarm_{side}"] = qmul(qinv(clav), new_world_rot[f"upperarm_{side}"])
        new_local_rot[f"lowerarm_{side}"] = qmul(qinv(new_world_rot[f"upperarm_{side}"]), new_world_rot[f"lowerarm_{side}"])
        new_local_rot[f"hand_{side}"] = qmul(qinv(new_world_rot[f"lowerarm_{side}"]), new_world_rot[f"hand_{side}"])

    for chain in FINGER_CHAINS:
        right_parent, left_parent = hand_r_rot, hand_l_rot
        for bone in chain:
            right_world = qmul(right_parent, q(local[f"{bone}_r"].rotation))
            left_world = m_to_q(mmul(mmul(mmul(mirror, q_to_m(right_world)), MIRROR_X), FINGER_CORRECTION[bone]))
            new_local_rot[f"{bone}_l"] = qmul(qinv(left_parent), left_world)
            right_parent, left_parent = right_world, left_world

    # Hand IK bones: ik_hand_gun under ik_hand_root sits on the right hand, ik_hand_r on it with
    # an identity offset, ik_hand_l under ik_hand_gun on the left hand.
    new_local_pos = {}
    root_w = world["ik_hand_root"]
    root_rot, root_pos = q(root_w.rotation), v(root_w.translation)
    gun_rot = new_world_rot["hand_r"]
    gun_pos = new_world_pos["hand_r"]
    new_local_rot["ik_hand_gun"] = qmul(qinv(root_rot), gun_rot)
    new_local_pos["ik_hand_gun"] = qrot(qinv(root_rot), sub(gun_pos, root_pos))
    new_local_rot["ik_hand_l"] = qmul(qinv(gun_rot), new_world_rot["hand_l"])
    new_local_pos["ik_hand_l"] = qrot(qinv(gun_rot), sub(new_world_pos["hand_l"], gun_pos))

    for name in bone_names:
        lt = local.get(name)
        if lt is None:
            continue
        pos = new_local_pos.get(name, v(lt.translation))
        rot = new_local_rot.get(name, q(lt.rotation))
        keys[name][0].append(unreal.Vector(*pos))
        keys[name][1].append(to_uquat(rot))
        keys[name][2].append(lt.scale3d)

    if i in (0, FRAMES):
        log(f"frame {i}: hand_r {v(world['hand_r'].translation)} -> {new_world_pos['hand_r']}, "
            f"hand_l {v(world['hand_l'].translation)} -> {new_world_pos['hand_l']}")

written = 0
for name, (positions, rotations, scales) in keys.items():
    if len(positions) == FRAMES + 1:
        controller.set_bone_track_keys(name, positions, rotations, scales, False)
        written += 1
controller.close_bracket(False)
log(f"wrote {written} bone tracks, {FRAMES + 1} keys each, length {anim.get_play_length():.2f}s")

eal.save_loaded_asset(anim, only_if_is_dirty=False)

# Verify by evaluating the result: the hand should land where the IK put it.
check = APE.get_anim_pose_at_time(anim, anim.get_play_length(), options)
log(f"check end hand_r={v(APE.get_bone_pose(check, 'hand_r', unreal.AnimPoseSpaces.WORLD).translation)} "
    f"hand_l={v(APE.get_bone_pose(check, 'hand_l', unreal.AnimPoseSpaces.WORLD).translation)}")
log("done")

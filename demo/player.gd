extends CharacterBody3D
## Slim player controller.
##
## The player owns input, physics and the camera. It never decides which
## animation plays: it reports intent to the MotionMatchingController and the
## GDExtension does the rest.

@export var _motion_matching: MotionMatchingController
@export var _camera_pivot: Node3D
@export var _skeleton: Skeleton3D
@export var _foot_ik: MMFootIKModifier

@export var _walk_speed: float = 1.8
@export var _run_speed: float = 4.5
@export var _sprint_speed: float = 6.5
@export var _acceleration: float = 18.0
@export var _deceleration: float = 26.0
@export var _jump_velocity: float = 5.0
@export var _body_turn_threshold_deg: float = 70.0
@export var _mouse_sensitivity: float = 0.0025

var _gravity: float = 9.8
var _camera_yaw: float = 0.0
var _camera_pitch: float = 0.0
var _base_body_rotation: Basis
var _fall_start_height: float = 0.0
var _was_grounded: bool = true


func _ready() -> void:
	# Read the authored rotation from the editor instead of hardcoding it, so
	# a re-imported rig does not silently break the facing math.
	_base_body_rotation = global_transform.basis
	_gravity = ProjectSettings.get_setting("physics/3d/default_gravity", 9.8)
	Input.set_mouse_mode(Input.MOUSE_MODE_CAPTURED)


func _unhandled_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion and Input.get_mouse_mode() == Input.MOUSE_MODE_CAPTURED:
		_camera_yaw -= event.relative.x * _mouse_sensitivity
		_camera_pitch = clampf(_camera_pitch - event.relative.y * _mouse_sensitivity, -1.2, 1.2)


func _physics_process(delta: float) -> void:
	_update_camera()

	var input_vector := Input.get_vector("move_left", "move_right", "move_forward", "move_back")
	var target_speed := _walk_speed
	if Input.is_action_pressed("sprint"):
		target_speed = _sprint_speed
	elif input_vector.length() > 0.5:
		target_speed = _run_speed

	# Movement is relative to the camera, which is what makes strafing read
	# correctly with a decoupled aim.
	var camera_basis := Basis(Vector3.UP, _camera_yaw)
	var desired_direction := camera_basis * Vector3(input_vector.x, 0.0, input_vector.y)
	var desired_velocity := desired_direction * target_speed

	var rate := _acceleration if desired_velocity.length_squared() > 0.01 else _deceleration
	velocity.x = move_toward(velocity.x, desired_velocity.x, rate * delta)
	velocity.z = move_toward(velocity.z, desired_velocity.z, rate * delta)

	if not is_on_floor():
		velocity.y -= _gravity * delta
	elif Input.is_action_just_pressed("jump"):
		velocity.y = _jump_velocity
		if _motion_matching:
			_motion_matching.request_jump()

	_update_body_facing(desired_velocity, delta)
	_report_to_motion_matching(desired_velocity, delta)

	move_and_slide()


func _update_camera() -> void:
	if _camera_pivot:
		_camera_pivot.global_rotation = Vector3(_camera_pitch, _camera_yaw, 0.0)


## The camera never rotates the body. The body turns when it has drifted too
## far from where the player is looking, or continuously while moving.
func _update_body_facing(desired_velocity: Vector3, delta: float) -> void:
	var camera_forward := Basis(Vector3.UP, _camera_yaw) * Vector3.FORWARD
	var body_forward := -global_transform.basis.z
	var angle := absf(body_forward.signed_angle_to(camera_forward, Vector3.UP))

	if desired_velocity.length_squared() > 0.01:
		rotation.y = lerp_angle(rotation.y, _camera_yaw, 1.0 - exp(-12.0 * delta))
	elif angle > deg_to_rad(_body_turn_threshold_deg):
		rotation.y = lerp_angle(rotation.y, _camera_yaw, 1.0 - exp(-6.0 * delta))


func _report_to_motion_matching(desired_velocity: Vector3, delta: float) -> void:
	if not _motion_matching:
		return

	var grounded := is_on_floor()

	# Fall height decides which landing clip is appropriate.
	if not grounded and _was_grounded:
		_fall_start_height = global_position.y
	if not grounded:
		_motion_matching.set_fall_distance(maxf(0.0, _fall_start_height - global_position.y))
	_was_grounded = grounded

	_motion_matching.set_velocity(velocity)
	_motion_matching.set_desired_velocity(desired_velocity)
	_motion_matching.set_facing(-global_transform.basis.z)
	_motion_matching.set_direction(Basis(Vector3.UP, _camera_yaw) * Vector3.FORWARD)
	_motion_matching.set_ground_state(grounded)

	# Duck typing keeps the controller optional: any node exposing the same
	# method works, including a GDScript stand in during prototyping.
	if _foot_ik and _foot_ik.has_method("set_foot_contacts"):
		var info: Dictionary = _motion_matching.get_debug_info()
		_foot_ik.set_foot_contacts(info.get("left_foot_contact", false), info.get("right_foot_contact", false))

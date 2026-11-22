extends Node2D

# Fair, zero-GDScript-loop Godot 4 benchmark matching Platformator's exact scenarios:
#   - broad_phase: N moving bodies on non-overlapping horizontal lanes
#   - rigid_body_container: N dynamic bodies (50% box 28x28, 50% circle r=14) in a walled box
#   - ideal_world: N dynamic bodies in disjoint 20-body chambers (300x300, pitch 600)

const BROAD_START_X := [
	-1400.0, 1470.0, -1540.0, 1610.0, -1680.0, 1750.0, -1820.0, 1890.0,
	-1960.0, 2030.0, -2100.0, 2170.0, -2240.0, 2310.0, -2380.0, 2450.0,
]
const BROAD_VEL_X := [
	420.0, -500.0, 580.0, -660.0, 740.0, -820.0, 900.0, -980.0,
	1060.0, -1140.0, 1220.0, -1300.0, 1380.0, -1460.0, 1540.0, -1620.0,
]
const RBC_VX := [-48.0, 36.0, -28.0, 44.0, -40.0, 24.0, -32.0, 52.0]
const RBC_VY := [0.0, -18.0, -12.0, 8.0, -6.0, 14.0]
const RBC_OMEGA := [-1.4, 0.9, -0.7, 1.1, -1.2, 0.8, -0.6, 1.3]

var scenario := "rigid_body_container"
var count := 500
var warmup_frames := 120
var measure_frames := 600
var physics_fps := 120
var can_sleep := true
var use_server_only := false

var _tick := 0
var _last_tick_usec := 0
var _step_times_ms := PackedFloat64Array()
var _active_bodies_sum := 0.0
var _collision_pairs_sum := 0.0
var _spawned_dynamic := 0
var _spawned_static := 0
var _server_rids: Array[RID] = []


func _ready() -> void:
	_parse_args()
	Engine.physics_ticks_per_second = physics_fps
	Engine.max_fps = 0
	_step_times_ms.resize(measure_frames)

	match scenario:
		"broad_phase":
			_build_broad_phase()
		"rigid_body_container":
			_build_rigid_body_container()
		"ideal_world":
			_build_ideal_world()
		_:
			push_error("Unknown scenario: " + scenario)
			get_tree().quit(1)
			return

	set_physics_process(true)
	_last_tick_usec = Time.get_ticks_usec()


func _parse_args() -> void:
	var args := OS.get_cmdline_user_args()
	var i := 0
	while i < args.size():
		match args[i]:
			"--scenario":
				i += 1
				scenario = args[i]
			"--count":
				i += 1
				count = args[i].to_int()
			"--warmup-frames":
				i += 1
				warmup_frames = args[i].to_int()
			"--measure-frames":
				i += 1
				measure_frames = args[i].to_int()
			"--physics-fps":
				i += 1
				physics_fps = args[i].to_int()
			"--can-sleep":
				i += 1
				can_sleep = (args[i] == "true" or args[i] == "1")
			"--server-only":
				use_server_only = true
		i += 1


func _physics_process(_delta: float) -> void:
	var now_usec := Time.get_ticks_usec()
	var dt_ms := float(now_usec - _last_tick_usec) * 0.001
	_last_tick_usec = now_usec

	if _tick >= warmup_frames:
		var idx := _tick - warmup_frames
		if idx < measure_frames:
			_step_times_ms[idx] = dt_ms
			_active_bodies_sum += Performance.get_monitor(Performance.PHYSICS_2D_ACTIVE_OBJECTS)
			_collision_pairs_sum += Performance.get_monitor(Performance.PHYSICS_2D_COLLISION_PAIRS)
		if idx + 1 >= measure_frames:
			_finish()
			return

	_tick += 1


func _finish() -> void:
	set_physics_process(false)
	var arr := Array(_step_times_ms)
	arr.sort()
	var total := 0.0
	for v in _step_times_ms:
		total += v
	var mean_ms := total / float(measure_frames)
	var median_ms: float = arr[measure_frames / 2]
	var p95_idx := clampi(int(ceil(0.95 * float(measure_frames))) - 1, 0, measure_frames - 1)
	var p95_ms: float = arr[p95_idx]
	var avg_active := _active_bodies_sum / float(measure_frames)
	var avg_pairs := _collision_pairs_sum / float(measure_frames)
	var perf_phys_ms := Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS) * 1000.0
	var ver := Engine.get_version_info()
	var ver_str := "godot_%d.%d.%d" % [ver["major"], ver["minor"], ver["patch"]]

	print(JSON.stringify({
		"engine": ver_str,
		"scenario": scenario,
		"count": count,
		"warmup_frames": warmup_frames,
		"measure_frames": measure_frames,
		"can_sleep": can_sleep,
		"server_only": use_server_only,
		"dynamic_bodies": _spawned_dynamic,
		"static_bodies": _spawned_static,
		"mean_ms": snappedf(mean_ms, 0.0001),
		"median_ms": snappedf(median_ms, 0.0001),
		"p95_ms": snappedf(p95_ms, 0.0001),
		"perf_physics_ms": snappedf(perf_phys_ms, 0.0001),
		"avg_active_bodies": snappedf(avg_active, 0.1),
		"avg_collision_pairs": snappedf(avg_pairs, 0.1),
	}))

	for rid in _server_rids:
		PhysicsServer2D.free_rid(rid)
	_server_rids.clear()

	get_tree().quit(0)


func _build_broad_phase() -> void:
	const LANE_STEP_Y := 80.0
	const LANE_SIZE := Vector2(48.0, 48.0)
	var lane_start_y := -0.5 * float(count - 1) * LANE_STEP_Y

	if use_server_only:
		var space := get_world_2d().space
		var shape_rid := PhysicsServer2D.rectangle_shape_create()
		PhysicsServer2D.shape_set_data(shape_rid, LANE_SIZE * 0.5)
		_server_rids.append(shape_rid)
		for i in range(count):
			var p := i % BROAD_START_X.size()
			var pos := Vector2(BROAD_START_X[p], lane_start_y + float(i) * LANE_STEP_Y)
			var body_rid := PhysicsServer2D.body_create()
			PhysicsServer2D.body_set_mode(body_rid, PhysicsServer2D.BODY_MODE_RIGID)
			PhysicsServer2D.body_set_space(body_rid, space)
			PhysicsServer2D.body_add_shape(body_rid, shape_rid)
			PhysicsServer2D.body_set_param(body_rid, PhysicsServer2D.BODY_PARAM_GRAVITY_SCALE, 0.0)
			PhysicsServer2D.body_set_param(body_rid, PhysicsServer2D.BODY_PARAM_LINEAR_DAMP, 0.0)
			PhysicsServer2D.body_set_param(body_rid, PhysicsServer2D.BODY_PARAM_ANGULAR_DAMP, 0.0)
			PhysicsServer2D.body_set_state(body_rid, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0.0, pos))
			PhysicsServer2D.body_set_state(body_rid, PhysicsServer2D.BODY_STATE_LINEAR_VELOCITY, Vector2(BROAD_VEL_X[p], 0.0))
			PhysicsServer2D.body_set_state(body_rid, PhysicsServer2D.BODY_STATE_CAN_SLEEP, false)
			_server_rids.append(body_rid)
			_spawned_dynamic += 1
		return

	var shared_rect := RectangleShape2D.new()
	shared_rect.size = LANE_SIZE
	for i in range(count):
		var p := i % BROAD_START_X.size()
		var body := RigidBody2D.new()
		body.position = Vector2(BROAD_START_X[p], lane_start_y + float(i) * LANE_STEP_Y)
		body.linear_velocity = Vector2(BROAD_VEL_X[p], 0.0)
		body.gravity_scale = 0.0
		body.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
		body.linear_damp = 0.0
		body.angular_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
		body.angular_damp = 0.0
		body.can_sleep = false
		body.collision_layer = 1
		body.collision_mask = 1
		var cs := CollisionShape2D.new()
		cs.shape = shared_rect
		body.add_child(cs)
		add_child(body)
		_spawned_dynamic += 1


func _build_rigid_body_container() -> void:
	const BOX_SIZE := Vector2(28.0, 28.0)
	const CIRCLE_RADIUS := 14.0
	const WALL_THICKNESS := 64.0
	const SPAWN_MARGIN := 10.0
	const CELL_PADDING := 10.0
	const MIN_W := 1600.0
	const MIN_H := 900.0

	var box_count := count / 2
	var circle_count := count - box_count
	var cell_size := 28.0 + CELL_PADDING
	var usable_w := maxf(MIN_W - 2.0 * SPAWN_MARGIN, cell_size)
	var cols := maxi(1, int(usable_w / cell_size))
	var rows := maxi(1, (count + cols - 1) / cols)
	var interior_w := maxf(MIN_W, float(cols) * cell_size + 2.0 * SPAWN_MARGIN)
	var interior_h := maxf(MIN_H, float(rows) * cell_size + 2.0 * SPAWN_MARGIN)
	var left := -0.5 * interior_w
	var top := -0.5 * interior_h
	var half_w := 0.5 * interior_w
	var half_h := 0.5 * interior_h

	var mat := PhysicsMaterial.new()
	mat.friction = 1.0
	mat.bounce = 0.0

	var horiz_size := Vector2(interior_w + 2.0 * WALL_THICKNESS, WALL_THICKNESS)
	var vert_size := Vector2(WALL_THICKNESS, interior_h + 2.0 * WALL_THICKNESS)

	if use_server_only:
		var space := get_world_2d().space
		_add_server_wall(space, Vector2(0.0, half_h + 0.5 * WALL_THICKNESS + 20.0), horiz_size, 1.0, 0.0)
		_add_server_wall(space, Vector2(0.0, -half_h - 0.5 * WALL_THICKNESS), horiz_size, 1.0, 0.0)
		_add_server_wall(space, Vector2(-half_w - 0.5 * WALL_THICKNESS, 0.0), vert_size, 1.0, 0.0)
		_add_server_wall(space, Vector2(half_w + 0.5 * WALL_THICKNESS, 0.0), vert_size, 1.0, 0.0)

		var box_rid := PhysicsServer2D.rectangle_shape_create()
		PhysicsServer2D.shape_set_data(box_rid, BOX_SIZE * 0.5)
		var circle_rid := PhysicsServer2D.circle_shape_create()
		PhysicsServer2D.shape_set_data(circle_rid, CIRCLE_RADIUS)
		_server_rids.append(box_rid)
		_server_rids.append(circle_rid)

		var sb := 0
		var sc := 0
		var idx := 0
		while sb < box_count or sc < circle_count:
			var is_box := sb < box_count and (sc >= circle_count or idx % 2 == 0)
			var col := idx % cols
			var row := idx / cols
			var x := left + SPAWN_MARGIN + (float(col) + 0.5) * cell_size + float((idx % 3) - 1) * 2.0
			var y := top + SPAWN_MARGIN + (float(row) + 0.5) * cell_size + float((idx % 4) - 1) * 1.5
			var vel := Vector2(RBC_VX[idx % RBC_VX.size()], RBC_VY[idx % RBC_VY.size()])
			var omega: float = RBC_OMEGA[idx % RBC_OMEGA.size()]
			_add_server_dynamic(space, box_rid if is_box else circle_rid, Vector2(x, y), vel, omega, 1.0, 0.0)
			if is_box:
				sb += 1
			else:
				sc += 1
			idx += 1
		return

	_add_node_wall(Vector2(0.0, half_h + 0.5 * WALL_THICKNESS + 20.0), horiz_size, mat)
	_add_node_wall(Vector2(0.0, -half_h - 0.5 * WALL_THICKNESS), horiz_size, mat)
	_add_node_wall(Vector2(-half_w - 0.5 * WALL_THICKNESS, 0.0), vert_size, mat)
	_add_node_wall(Vector2(half_w + 0.5 * WALL_THICKNESS, 0.0), vert_size, mat)

	var shared_box := RectangleShape2D.new()
	shared_box.size = BOX_SIZE
	var shared_circle := CircleShape2D.new()
	shared_circle.radius = CIRCLE_RADIUS

	var sb := 0
	var sc := 0
	var idx := 0
	while sb < box_count or sc < circle_count:
		var is_box := sb < box_count and (sc >= circle_count or idx % 2 == 0)
		var col := idx % cols
		var row := idx / cols
		var x := left + SPAWN_MARGIN + (float(col) + 0.5) * cell_size + float((idx % 3) - 1) * 2.0
		var y := top + SPAWN_MARGIN + (float(row) + 0.5) * cell_size + float((idx % 4) - 1) * 1.5
		var vel := Vector2(RBC_VX[idx % RBC_VX.size()], RBC_VY[idx % RBC_VY.size()])
		var omega: float = RBC_OMEGA[idx % RBC_OMEGA.size()]
		_add_node_dynamic(shared_box if is_box else shared_circle, Vector2(x, y), vel, omega, mat)
		if is_box:
			sb += 1
		else:
			sc += 1
		idx += 1


func _build_ideal_world() -> void:
	const BODIES_PER_CHAMBER := 20
	const CHAMBER_INTERIOR := 300.0
	const WALL_THICKNESS := 24.0
	const CHAMBER_PITCH := 600.0
	const BOX_SIZE := Vector2(24.0, 24.0)
	const CIRCLE_RADIUS := 12.0
	const BODY_STEP := 46.0
	const FRICTION := 0.8
	const RESTITUTION := 0.15

	var chamber_count := (count + BODIES_PER_CHAMBER - 1) / BODIES_PER_CHAMBER
	var chamber_cols := maxi(1, int(ceil(sqrt(float(chamber_count)))))
	var chamber_rows := maxi(1, (chamber_count + chamber_cols - 1) / chamber_cols)
	var origin_x := -0.5 * float(chamber_cols - 1) * CHAMBER_PITCH
	var origin_y := -0.5 * float(chamber_rows - 1) * CHAMBER_PITCH
	var half_interior := 0.5 * CHAMBER_INTERIOR
	var full_span := CHAMBER_INTERIOR + 2.0 * WALL_THICKNESS

	var mat := PhysicsMaterial.new()
	mat.friction = FRICTION
	mat.bounce = RESTITUTION

	var shared_hwall := RectangleShape2D.new()
	shared_hwall.size = Vector2(full_span, WALL_THICKNESS)
	var shared_vwall := RectangleShape2D.new()
	shared_vwall.size = Vector2(WALL_THICKNESS, full_span)
	var shared_box := RectangleShape2D.new()
	shared_box.size = BOX_SIZE
	var shared_circle := CircleShape2D.new()
	shared_circle.radius = CIRCLE_RADIUS

	var space := get_world_2d().space
	var hwall_rid: RID
	var vwall_rid: RID
	var box_rid: RID
	var circle_rid: RID
	if use_server_only:
		hwall_rid = PhysicsServer2D.rectangle_shape_create()
		PhysicsServer2D.shape_set_data(hwall_rid, Vector2(full_span, WALL_THICKNESS) * 0.5)
		vwall_rid = PhysicsServer2D.rectangle_shape_create()
		PhysicsServer2D.shape_set_data(vwall_rid, Vector2(WALL_THICKNESS, full_span) * 0.5)
		box_rid = PhysicsServer2D.rectangle_shape_create()
		PhysicsServer2D.shape_set_data(box_rid, BOX_SIZE * 0.5)
		circle_rid = PhysicsServer2D.circle_shape_create()
		PhysicsServer2D.shape_set_data(circle_rid, CIRCLE_RADIUS)
		_server_rids.append_array([hwall_rid, vwall_rid, box_rid, circle_rid])

	var spawned := 0
	for c in range(chamber_count):
		var cx := origin_x + float(c % chamber_cols) * CHAMBER_PITCH
		var cy := origin_y + float(c / chamber_cols) * CHAMBER_PITCH

		if use_server_only:
			_add_server_wall_shape(space, hwall_rid, Vector2(cx, cy + half_interior + 0.5 * WALL_THICKNESS), FRICTION, RESTITUTION)
			_add_server_wall_shape(space, hwall_rid, Vector2(cx, cy - half_interior - 0.5 * WALL_THICKNESS), FRICTION, RESTITUTION)
			_add_server_wall_shape(space, vwall_rid, Vector2(cx - half_interior - 0.5 * WALL_THICKNESS, cy), FRICTION, RESTITUTION)
			_add_server_wall_shape(space, vwall_rid, Vector2(cx + half_interior + 0.5 * WALL_THICKNESS, cy), FRICTION, RESTITUTION)
		else:
			_add_node_wall_shape(shared_hwall, Vector2(cx, cy + half_interior + 0.5 * WALL_THICKNESS), mat)
			_add_node_wall_shape(shared_hwall, Vector2(cx, cy - half_interior - 0.5 * WALL_THICKNESS), mat)
			_add_node_wall_shape(shared_vwall, Vector2(cx - half_interior - 0.5 * WALL_THICKNESS, cy), mat)
			_add_node_wall_shape(shared_vwall, Vector2(cx + half_interior + 0.5 * WALL_THICKNESS, cy), mat)

		for local_idx in range(BODIES_PER_CHAMBER):
			if spawned >= count:
				break
			var bcol := local_idx % 5
			var brow := local_idx / 5
			var jx := float((spawned % 3) - 1) * 1.5
			var jy := float((spawned % 4) - 1) * 1.0
			var bx := cx + (float(bcol) - 2.0) * BODY_STEP + jx
			var by := cy + (float(brow) - 1.5) * BODY_STEP + jy
			var vel := Vector2(RBC_VX[spawned % RBC_VX.size()], RBC_VY[spawned % RBC_VY.size()])
			var omega: float = RBC_OMEGA[spawned % RBC_OMEGA.size()]

			if use_server_only:
				_add_server_dynamic(space, box_rid if (spawned % 2 == 0) else circle_rid, Vector2(bx, by), vel, omega, FRICTION, RESTITUTION)
			else:
				_add_node_dynamic(shared_box if (spawned % 2 == 0) else shared_circle, Vector2(bx, by), vel, omega, mat)
			spawned += 1


func _add_node_wall(pos: Vector2, size: Vector2, mat: PhysicsMaterial) -> void:
	var shape := RectangleShape2D.new()
	shape.size = size
	_add_node_wall_shape(shape, pos, mat)


func _add_node_wall_shape(shape: Shape2D, pos: Vector2, mat: PhysicsMaterial) -> void:
	var wall := StaticBody2D.new()
	wall.position = pos
	wall.collision_layer = 1
	wall.collision_mask = 1
	wall.physics_material_override = mat
	var cs := CollisionShape2D.new()
	cs.shape = shape
	wall.add_child(cs)
	add_child(wall)
	_spawned_static += 1


func _add_node_dynamic(shape: Shape2D, pos: Vector2, vel: Vector2, omega: float, mat: PhysicsMaterial) -> void:
	var body := RigidBody2D.new()
	body.position = pos
	body.linear_velocity = vel
	body.angular_velocity = omega
	body.collision_layer = 1
	body.collision_mask = 1
	body.gravity_scale = 1.0
	body.linear_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	body.linear_damp = 0.0
	body.angular_damp_mode = RigidBody2D.DAMP_MODE_REPLACE
	body.angular_damp = 0.0
	body.can_sleep = can_sleep
	body.physics_material_override = mat
	var cs := CollisionShape2D.new()
	cs.shape = shape
	body.add_child(cs)
	add_child(body)
	_spawned_dynamic += 1


func _add_server_wall(space: RID, pos: Vector2, size: Vector2, friction: float, bounce: float) -> void:
	var shape_rid := PhysicsServer2D.rectangle_shape_create()
	PhysicsServer2D.shape_set_data(shape_rid, size * 0.5)
	_server_rids.append(shape_rid)
	_add_server_wall_shape(space, shape_rid, pos, friction, bounce)


func _add_server_wall_shape(space: RID, shape_rid: RID, pos: Vector2, friction: float, bounce: float) -> void:
	var body_rid := PhysicsServer2D.body_create()
	PhysicsServer2D.body_set_mode(body_rid, PhysicsServer2D.BODY_MODE_STATIC)
	PhysicsServer2D.body_set_space(body_rid, space)
	PhysicsServer2D.body_add_shape(body_rid, shape_rid)
	PhysicsServer2D.body_set_param(body_rid, PhysicsServer2D.BODY_PARAM_FRICTION, friction)
	PhysicsServer2D.body_set_param(body_rid, PhysicsServer2D.BODY_PARAM_BOUNCE, bounce)
	PhysicsServer2D.body_set_state(body_rid, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0.0, pos))
	_server_rids.append(body_rid)
	_spawned_static += 1


func _add_server_dynamic(space: RID, shape_rid: RID, pos: Vector2, vel: Vector2, omega: float, friction: float, bounce: float) -> void:
	var body_rid := PhysicsServer2D.body_create()
	PhysicsServer2D.body_set_mode(body_rid, PhysicsServer2D.BODY_MODE_RIGID)
	PhysicsServer2D.body_set_space(body_rid, space)
	PhysicsServer2D.body_add_shape(body_rid, shape_rid)
	PhysicsServer2D.body_set_param(body_rid, PhysicsServer2D.BODY_PARAM_FRICTION, friction)
	PhysicsServer2D.body_set_param(body_rid, PhysicsServer2D.BODY_PARAM_BOUNCE, bounce)
	PhysicsServer2D.body_set_param(body_rid, PhysicsServer2D.BODY_PARAM_LINEAR_DAMP, 0.0)
	PhysicsServer2D.body_set_param(body_rid, PhysicsServer2D.BODY_PARAM_ANGULAR_DAMP, 0.0)
	PhysicsServer2D.body_set_state(body_rid, PhysicsServer2D.BODY_STATE_TRANSFORM, Transform2D(0.0, pos))
	PhysicsServer2D.body_set_state(body_rid, PhysicsServer2D.BODY_STATE_LINEAR_VELOCITY, vel)
	PhysicsServer2D.body_set_state(body_rid, PhysicsServer2D.BODY_STATE_ANGULAR_VELOCITY, omega)
	PhysicsServer2D.body_set_state(body_rid, PhysicsServer2D.BODY_STATE_CAN_SLEEP, can_sleep)
	_server_rids.append(body_rid)
	_spawned_dynamic += 1

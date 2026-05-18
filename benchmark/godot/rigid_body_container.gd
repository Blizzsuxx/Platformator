extends "res://benchmark_case.gd"

const DEFAULT_BOX_COUNT := 400
const DEFAULT_CIRCLE_COUNT := 400

const BOX_SIZE := Vector2(28.0, 28.0)
const CIRCLE_RADIUS := 14.0
const WALL_THICKNESS := 64.0
const SPAWN_MARGIN := 80.0
const CELL_PADDING := 8.0
const MINIMUM_INTERIOR_WIDTH := 1600.0
const MINIMUM_INTERIOR_HEIGHT := 900.0
const CAMERA_MARGIN := 160.0
const BODY_FRICTION := 0.45
const BODY_RESTITUTION := 0.0
const BOX_COLOR := Color(0.16, 0.44, 0.93, 0.88)
const CIRCLE_COLOR := Color(0.93, 0.33, 0.25, 0.88)
const WALL_COLOR := Color(0.17, 0.17, 0.19, 0.92)
const CIRCLE_VISUAL_SEGMENTS := 20
const VELOCITY_DAMPING := 0.25

var box_count := DEFAULT_BOX_COUNT
var circle_count := DEFAULT_CIRCLE_COUNT

var _column_count := 1
var _cell_size := 0.0
var _interior_size := Vector2.ZERO
var _interior_top_left := Vector2.ZERO
var _spawned_body_total := 0
var _body_material : PhysicsMaterial
var _dynamic_bodies: Array[RigidBody2D] = []


func _benchmark_name() -> String:
	return "rigid_body_container"


func _spawned_object_count() -> int:
	return _spawned_body_total + 4


func _moving_object_count() -> int:
	return _spawned_body_total


func _parse_custom_argument(arguments: PackedStringArray, index: int, argument: String) -> int:
	match argument:
		"--box-count":
			index += 1
			box_count = _parse_positive_int(arguments, index, argument, true)
			return index
		"--circle-count":
			index += 1
			circle_count = _parse_positive_int(arguments, index, argument, true)
			return index

	return -1


func _setup_benchmark() -> void:
	if box_count + circle_count <= 0:
		push_error("rigid_body_container requires at least one box or circle.")
		get_tree().quit(1)
		return

	_spawned_body_total = box_count + circle_count
	_body_material = PhysicsMaterial.new()
	_body_material.friction = BODY_FRICTION
	_body_material.bounce = BODY_RESTITUTION
	_dynamic_bodies.clear()

	_calculate_layout()
	_create_container_walls()
	_spawn_bodies()
	_configure_camera()


func _print_summary() -> void:
	super._print_summary()
	print(
		"[Benchmark][Scenario] box_count=%d circle_count=%d container_width=%.1f container_height=%.1f"
		% [box_count, circle_count, _interior_size.x, _interior_size.y]
	)


func _calculate_layout() -> void:
	var max_body_extent := maxf(maxf(BOX_SIZE.x, BOX_SIZE.y), CIRCLE_RADIUS * 2.0)
	_cell_size = max_body_extent + CELL_PADDING
	var usable_width := maxf(MINIMUM_INTERIOR_WIDTH - 2.0 * SPAWN_MARGIN, _cell_size)
	_column_count = maxi(1, int(floor(usable_width / _cell_size)))
	var row_count := maxi(1, int(ceil(float(_spawned_body_total) / float(_column_count))))
	var interior_width := maxf(MINIMUM_INTERIOR_WIDTH, float(_column_count) * _cell_size + 2.0 * SPAWN_MARGIN)
	var interior_height := maxf(MINIMUM_INTERIOR_HEIGHT, float(row_count) * _cell_size + 2.0 * SPAWN_MARGIN)
	_interior_size = Vector2(interior_width, interior_height)
	_interior_top_left = Vector2(-0.5 * interior_width, -0.5 * interior_height)


func _create_container_walls() -> void:
	var half_width := _interior_size.x * 0.5
	var half_height := _interior_size.y * 0.5
	var horizontal_wall_size := Vector2(_interior_size.x + 2.0 * WALL_THICKNESS, WALL_THICKNESS)
	var vertical_wall_size := Vector2(WALL_THICKNESS, _interior_size.y + 2.0 * WALL_THICKNESS)

	_create_wall("Container Floor", Vector2(0.0, half_height + WALL_THICKNESS * 0.5), horizontal_wall_size)
	_create_wall("Container Ceiling", Vector2(0.0, -half_height - WALL_THICKNESS * 0.5), horizontal_wall_size)
	_create_wall("Container Left Wall", Vector2(-half_width - WALL_THICKNESS * 0.5, 0.0), vertical_wall_size)
	_create_wall("Container Right Wall", Vector2(half_width + WALL_THICKNESS * 0.5, 0.0), vertical_wall_size)


func _spawn_bodies() -> void:
	var spawned_boxes := 0
	var spawned_circles := 0
	var spawn_index := 0

	while spawned_boxes < box_count or spawned_circles < circle_count:
		var should_spawn_box := spawned_boxes < box_count and (spawned_circles >= circle_count or spawn_index % 2 == 0)
		var position := _calculate_spawn_position(spawn_index)
		var velocity := _calculate_initial_velocity(spawn_index)
		var angular_velocity := 0.0

		if should_spawn_box:
			_create_dynamic_box("Container Box %02d" % spawned_boxes, position, velocity, angular_velocity)
			spawned_boxes += 1
		else:
			_create_dynamic_circle("Container Circle %02d" % spawned_circles, position, velocity, angular_velocity)
			spawned_circles += 1

		spawn_index += 1


func _post_step_benchmark() -> void:
	var left := _interior_top_left.x
	var right := _interior_top_left.x + _interior_size.x
	var top := _interior_top_left.y
	var bottom := _interior_top_left.y + _interior_size.y

	for body in _dynamic_bodies:
		if body == null:
			continue

		var extent := _calculate_body_extent(body)
		var position := body.global_position
		var velocity := body.linear_velocity

		if not position.is_finite() or not velocity.is_finite():
			body.global_position = Vector2(0.0, 0.5 * (top + bottom))
			body.linear_velocity = Vector2.ZERO
			body.angular_velocity = 0.0
			continue

		var clamped := false
		var min_x := left + extent
		var max_x := right - extent
		var min_y := top + extent
		var max_y := bottom - extent

		if position.x < min_x:
			position.x = min_x
			velocity.x = absf(velocity.x) * VELOCITY_DAMPING
			clamped = true
		elif position.x > max_x:
			position.x = max_x
			velocity.x = -absf(velocity.x) * VELOCITY_DAMPING
			clamped = true

		if position.y < min_y:
			position.y = min_y
			velocity.y = absf(velocity.y) * VELOCITY_DAMPING
			clamped = true
		elif position.y > max_y:
			position.y = max_y
			velocity.y = -absf(velocity.y) * VELOCITY_DAMPING
			clamped = true

		if not clamped:
			continue

		body.global_position = position
		body.linear_velocity = velocity
		body.angular_velocity = 0.0


func _configure_camera() -> void:
	var camera := Camera2D.new()
	camera.name = "BenchmarkCamera"
	camera.enabled = true
	camera.position = Vector2.ZERO
	add_child(camera)

	var viewport_size := get_viewport_rect().size
	var view_size := _interior_size + Vector2.ONE * (2.0 * (WALL_THICKNESS + CAMERA_MARGIN))
	var zoom_factor := minf(
		1.0,
		minf(
			viewport_size.x / maxf(view_size.x, 1.0),
			viewport_size.y / maxf(view_size.y, 1.0)
		)
	)
	camera.zoom = Vector2(zoom_factor, zoom_factor)


func _calculate_spawn_position(spawn_index: int) -> Vector2:
	var effective_column_count := maxi(1, mini(_spawned_body_total, _column_count))
	var column_index := spawn_index % effective_column_count
	var row_index := spawn_index / effective_column_count
	var row_width := float(effective_column_count) * _cell_size
	var x := -0.5 * row_width + (float(column_index) + 0.5) * _cell_size
	var bottom := _interior_top_left.y + _interior_size.y
	var y := bottom - (float(row_index) + 0.5) * _cell_size
	return Vector2(x, y)


func _calculate_initial_velocity(spawn_index: int) -> Vector2:
	return Vector2.ZERO


func _calculate_body_extent(body: RigidBody2D) -> float:
	if body.get_node_or_null("CollisionShape2D") == null:
		return 0.0

	var collision_shape := body.get_node("CollisionShape2D") as CollisionShape2D
	if collision_shape == null or collision_shape.shape == null:
		return 0.0

	if collision_shape.shape is CircleShape2D:
		return (collision_shape.shape as CircleShape2D).radius

	if collision_shape.shape is RectangleShape2D:
		var size := (collision_shape.shape as RectangleShape2D).size
		return 0.5 * size.length()

	return 0.0


func _create_wall(node_name: String, position: Vector2, size: Vector2) -> void:
	var wall := StaticBody2D.new()
	wall.name = node_name
	wall.position = position
	wall.collision_layer = 1
	wall.collision_mask = 1
	wall.physics_material_override = _body_material

	var collision_shape := CollisionShape2D.new()
	var rectangle_shape := RectangleShape2D.new()
	rectangle_shape.size = size
	collision_shape.shape = rectangle_shape
	wall.add_child(collision_shape)

	var visual := Polygon2D.new()
	visual.color = WALL_COLOR
	visual.polygon = PackedVector2Array([
		Vector2(-size.x * 0.5, -size.y * 0.5),
		Vector2(size.x * 0.5, -size.y * 0.5),
		Vector2(size.x * 0.5, size.y * 0.5),
		Vector2(-size.x * 0.5, size.y * 0.5),
	])
	wall.add_child(visual)

	add_child(wall)


func _create_dynamic_box(node_name: String, position: Vector2, velocity: Vector2, angular_velocity: float) -> void:
	var body := _create_dynamic_body(node_name, position, velocity, angular_velocity)

	var collision_shape := CollisionShape2D.new()
	var rectangle_shape := RectangleShape2D.new()
	rectangle_shape.size = BOX_SIZE
	collision_shape.shape = rectangle_shape
	body.add_child(collision_shape)

	var visual := Polygon2D.new()
	visual.color = BOX_COLOR
	visual.polygon = PackedVector2Array([
		Vector2(-BOX_SIZE.x * 0.5, -BOX_SIZE.y * 0.5),
		Vector2(BOX_SIZE.x * 0.5, -BOX_SIZE.y * 0.5),
		Vector2(BOX_SIZE.x * 0.5, BOX_SIZE.y * 0.5),
		Vector2(-BOX_SIZE.x * 0.5, BOX_SIZE.y * 0.5),
	])
	body.add_child(visual)

	add_child(body)


func _create_dynamic_circle(node_name: String, position: Vector2, velocity: Vector2, angular_velocity: float) -> void:
	var body := _create_dynamic_body(node_name, position, velocity, angular_velocity)

	var collision_shape := CollisionShape2D.new()
	var circle_shape := CircleShape2D.new()
	circle_shape.radius = CIRCLE_RADIUS
	collision_shape.shape = circle_shape
	body.add_child(collision_shape)

	var visual := Polygon2D.new()
	visual.color = CIRCLE_COLOR
	visual.polygon = _create_circle_polygon(CIRCLE_RADIUS, CIRCLE_VISUAL_SEGMENTS)
	body.add_child(visual)

	add_child(body)


func _create_dynamic_body(node_name: String, position: Vector2, velocity: Vector2, angular_velocity: float) -> RigidBody2D:
	var body := RigidBody2D.new()
	body.name = node_name
	body.position = position
	body.linear_velocity = velocity
	body.angular_velocity = angular_velocity
	body.collision_layer = 1
	body.collision_mask = 1
	body.gravity_scale = 1.0
	body.linear_damp = 0.0
	body.angular_damp = 0.0
	body.physics_material_override = _body_material
	_dynamic_bodies.append(body)
	return body


func _create_circle_polygon(radius: float, segment_count: int) -> PackedVector2Array:
	var points := PackedVector2Array()
	for segment_index in range(segment_count):
		var angle := TAU * float(segment_index) / float(segment_count)
		points.append(Vector2(cos(angle), sin(angle)) * radius)
	return points

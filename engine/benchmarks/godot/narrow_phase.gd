extends BenchmarkCase

const COLUMN_POSITIONS := [-125.0, -75.0, -25.0, 25.0, 75.0, 125.0]
const ROW_POSITIONS := [-125.0, -75.0, -25.0, 25.0, 75.0, 125.0]
const BAND_POSITIONS := [-140.0, -100.0, -60.0, -20.0, 20.0, 60.0, 100.0, 140.0]
const BAND_SPEEDS := [20.0, -20.0, 24.0, -24.0, 20.0, -20.0, 24.0, -24.0]

const COLUMN_SIZE := Vector2(26.0, 360.0)
const ROW_SIZE := Vector2(360.0, 26.0)
const HORIZONTAL_BAND_SIZE := Vector2(360.0, 18.0)
const VERTICAL_BAND_SIZE := Vector2(18.0, 360.0)

var _moving_areas: Array[Area2D] = []
var _velocities: Array[Vector2] = []
var _spawned_object_total := 0


func _benchmark_name() -> String:
	return "narrow_phase"


func _setup_benchmark() -> void:
	for index in range(COLUMN_POSITIONS.size()):
		_create_area("Trigger Column %02d" % index, Vector2(COLUMN_POSITIONS[index], 0.0), COLUMN_SIZE)

	for index in range(ROW_POSITIONS.size()):
		_create_area("Trigger Row %02d" % index, Vector2(0.0, ROW_POSITIONS[index]), ROW_SIZE)

	for index in range(BAND_POSITIONS.size()):
		_spawn_moving_area(
			"Horizontal Band %02d" % index,
			Vector2(0.0, BAND_POSITIONS[index]),
			Vector2(0.0, BAND_SPEEDS[index]),
			HORIZONTAL_BAND_SIZE
		)

	for index in range(BAND_POSITIONS.size()):
		_spawn_moving_area(
			"Vertical Band %02d" % index,
			Vector2(BAND_POSITIONS[index], 0.0),
			Vector2(BAND_SPEEDS[index], 0.0),
			VERTICAL_BAND_SIZE
		)


func _advance_benchmark(delta: float) -> void:
	for index in range(_moving_areas.size()):
		_moving_areas[index].position += _velocities[index] * delta


func _spawned_object_count() -> int:
	return _spawned_object_total


func _moving_object_count() -> int:
	return _moving_areas.size()


func _spawn_moving_area(node_name: String, position: Vector2, velocity: Vector2, size: Vector2) -> void:
	var area := _create_area(node_name, position, size)
	_moving_areas.append(area)
	_velocities.append(velocity)


func _create_area(node_name: String, position: Vector2, size: Vector2) -> Area2D:
	var area := Area2D.new()
	area.name = node_name
	area.position = position
	area.collision_layer = 1
	area.collision_mask = 1
	area.monitoring = true
	area.monitorable = true

	var collision_shape := CollisionShape2D.new()
	var rectangle_shape := RectangleShape2D.new()
	rectangle_shape.size = size
	collision_shape.shape = rectangle_shape
	area.add_child(collision_shape)

	add_child(area)
	_spawned_object_total += 1
	return area

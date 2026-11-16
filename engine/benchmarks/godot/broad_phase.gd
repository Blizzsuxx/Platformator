extends "res://benchmark_case.gd"

const LANE_COUNT := 128
const LANE_START_Y := -1240.0
const LANE_STEP_Y := 80.0
const LANE_WIDTH := 48.0
const LANE_HEIGHT := 48.0
const START_X_PATTERN := [-1400.0, 1470.0, -1540.0, 1610.0, -1680.0, 1750.0, -1820.0, 1890.0, -1960.0, 2030.0, -2100.0, 2170.0, -2240.0, 2310.0, -2380.0, 2450.0]
const VELOCITY_X_PATTERN := [420.0, -500.0, 580.0, -660.0, 740.0, -820.0, 900.0, -980.0, 1060.0, -1140.0, 1220.0, -1300.0, 1380.0, -1460.0, 1540.0, -1620.0]

var _moving_areas: Array[Area2D] = []
var _velocities: Array[Vector2] = []


func _benchmark_name() -> String:
	return "broad_phase"


func _setup_benchmark() -> void:
	for lane_index in range(LANE_COUNT):
		var pattern_index := lane_index % START_X_PATTERN.size()
		var position := Vector2(START_X_PATTERN[pattern_index], LANE_START_Y + float(lane_index) * LANE_STEP_Y)
		var velocity := Vector2(VELOCITY_X_PATTERN[pattern_index], 0.0)
		_spawn_moving_area(position, velocity, Vector2(LANE_WIDTH, LANE_HEIGHT), "Lane Mover %02d" % lane_index)


func _advance_benchmark(delta: float) -> void:
	for index in range(_moving_areas.size()):
		_moving_areas[index].position += _velocities[index] * delta


func _spawned_object_count() -> int:
	return _moving_areas.size()


func _moving_object_count() -> int:
	return _moving_areas.size()


func _spawn_moving_area(position: Vector2, velocity: Vector2, size: Vector2, node_name: String) -> void:
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
	return area

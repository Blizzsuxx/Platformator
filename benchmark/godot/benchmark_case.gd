extends Node2D

class_name BenchmarkCase

const DEFAULT_WARMUP_FRAMES := 120
const DEFAULT_MEASURE_FRAMES := 600
const DEFAULT_PHYSICS_TICKS_PER_SECOND := 120

var warmup_frames := DEFAULT_WARMUP_FRAMES
var measure_frames := DEFAULT_MEASURE_FRAMES
var physics_ticks_per_second := DEFAULT_PHYSICS_TICKS_PER_SECOND

var _tick_index := 0
var _measure_index := 0
var _frame_time_samples: Array[float] = []
var _physics_time_samples: Array[float] = []
var _object_count_samples: Array[float] = []
var _node_count_samples: Array[float] = []
var _collision_pair_samples: Array[float] = []


func _ready() -> void:
	if not _parse_command_line():
		return

	Engine.physics_ticks_per_second = physics_ticks_per_second
	Engine.max_fps = 0
	set_physics_process(true)

	_frame_time_samples.clear()
	_physics_time_samples.clear()
	_object_count_samples.clear()
	_node_count_samples.clear()
	_collision_pair_samples.clear()

	_setup_benchmark()


func _physics_process(delta: float) -> void:
	_capture_sample()
	if _measure_index == measure_frames:
		_print_summary()
		set_physics_process(false)
		get_tree().quit()
		return

	_advance_benchmark(delta)
	_post_step_benchmark()
	_tick_index += 1


func _setup_benchmark() -> void:
	assert(false, "BenchmarkCase subclasses must override _setup_benchmark().")


func _advance_benchmark(_delta: float) -> void:
	pass


func _post_step_benchmark() -> void:
	pass


func _benchmark_name() -> String:
	assert(false, "BenchmarkCase subclasses must override _benchmark_name().")
	return "unknown"


func _spawned_object_count() -> int:
	return 0


func _moving_object_count() -> int:
	return 0


func _parse_custom_argument(_arguments: PackedStringArray, _index: int, _argument: String) -> int:
	return -1


func _parse_command_line() -> bool:
	var arguments := OS.get_cmdline_user_args()
	var index := 0

	while index < arguments.size():
		var argument := arguments[index]
		match argument:
			"--warmup-frames":
				index += 1
				warmup_frames = _parse_positive_int(arguments, index, argument, true)
				if warmup_frames < 0:
					return false
			"--measure-frames":
				index += 1
				measure_frames = _parse_positive_int(arguments, index, argument, false)
				if measure_frames < 0:
					return false
			"--physics-fps":
				index += 1
				physics_ticks_per_second = _parse_positive_int(arguments, index, argument, false)
				if physics_ticks_per_second < 0:
					return false
			_:
				var custom_index := _parse_custom_argument(arguments, index, argument)
				if custom_index >= 0:
					index = custom_index
				else:
					push_error("Unknown benchmark argument: %s" % argument)
					get_tree().quit(1)
					return false

		index += 1

	return true


func _parse_positive_int(arguments: PackedStringArray, index: int, argument_name: String, allow_zero: bool) -> int:
	if index >= arguments.size():
		push_error("Missing value for %s" % argument_name)
		get_tree().quit(1)
		return -1

	var value := arguments[index].to_int()
	if allow_zero and value >= 0:
		return value
	if not allow_zero and value > 0:
		return value

	var condition := "greater than or equal to zero" if allow_zero else "greater than zero"
	push_error("%s must be %s" % [argument_name, condition])
	get_tree().quit(1)
	return -1


func _capture_sample() -> void:
	if _tick_index < warmup_frames:
		return

	_frame_time_samples.append(Performance.get_monitor(Performance.TIME_PROCESS))
	_physics_time_samples.append(Performance.get_monitor(Performance.TIME_PHYSICS_PROCESS))
	_object_count_samples.append(Performance.get_monitor(Performance.OBJECT_COUNT))
	_node_count_samples.append(Performance.get_monitor(Performance.OBJECT_NODE_COUNT))
	_collision_pair_samples.append(Performance.get_monitor(Performance.PHYSICS_2D_COLLISION_PAIRS))
	_measure_index += 1


func _print_summary() -> void:
	print("[Benchmark] scenario=%s frame_count=%d" % [_benchmark_name(), measure_frames])
	_print_time_summary("frame", _frame_time_samples)
	_print_time_summary("physics_process", _physics_time_samples)
	_print_counter_summary("object_count", _object_count_samples)
	_print_counter_summary("node_count", _node_count_samples)
	_print_counter_summary("physics_2d_collision_pairs", _collision_pair_samples)
	print("[Benchmark][Scenario] spawned_object_count=%d moving_object_count=%d" % [_spawned_object_count(), _moving_object_count()])


func _print_time_summary(label: String, samples: Array[float]) -> void:
	var sorted_samples := samples.duplicate()
	sorted_samples.sort()
	var average_value := _average(samples) * 1000.0
	var median_value : float = sorted_samples[sorted_samples.size() / 2] * 1000.0
	var p95_value : float = sorted_samples[_percentile_index(sorted_samples.size(), 0.95)] * 1000.0
	print("[Benchmark][Scope] %s avg_ms=%.3f median_ms=%.3f p95_ms=%.3f" % [label, average_value, median_value, p95_value])


func _print_counter_summary(label: String, samples: Array[float]) -> void:
	var average_value := _average(samples)
	var minimum_value := samples[0]
	var maximum_value := samples[0]

	for sample in samples:
		minimum_value = minf(minimum_value, sample)
		maximum_value = maxf(maximum_value, sample)

	print("[Benchmark][Counter] %s avg=%.3f min=%.0f max=%.0f" % [label, average_value, minimum_value, maximum_value])


func _average(samples: Array[float]) -> float:
	var total := 0.0
	for sample in samples:
		total += sample
	return total / float(samples.size())


func _percentile_index(sample_count: int, percentile: float) -> int:
	return maxi(int(ceil(percentile * float(sample_count))) - 1, 0)

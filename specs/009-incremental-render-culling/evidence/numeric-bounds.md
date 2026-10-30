# Certified Render Proxy and Query Bounds

## Domain and operations

The implementation uses `P = 2^14 = 16384` output pixels. A projection is certifiable only when both scales are finite normal positive floats, `4P / scale <= FLT_MAX` on each axis, and the output dimensions are positive and at most `P`. A sprite is certifiable only when its synchronized center and effective dimensions are finite, positive where required, and each of `|worldX|*scaleX`, `|worldY|*scaleY`, `width*scaleX`, and `height*scaleY` is at most `P`. The actual float products for rendered width and height must be finite normal values. Camera coordinates use the corresponding product bounds. The four-times pre-scale check is a conservative overflow guard: the exact horizontal/vertical expression has magnitude at most `2.5P/scale` before the final scale multiplication.

The draw path being enclosed is the existing operation order in `Camera::renderWithView`:

1. `left = (worldX - width*0.5f - cameraX) * scaleX` and the corresponding top expression, evaluated as float operations in that order.
2. `screenWidth = width*scaleX`, `screenHeight = height*scaleY`, then `right = left + screenWidth` and `bottom = top + screenHeight` in float.
3. For a nonzero angle, convert degrees with `float((SDL_PI_D * double(angle))/180)`, use `SDL_sinf`/`SDL_cosf`, form the float center, subtract each corner from that center, multiply offsets by sine/cosine, combine in SDL's order, and add the center.
4. `makePaddedBounds` takes the float corner extrema and expands them by `1 + 8*float_epsilon*maximumMagnitude`, with outward `double` `nextafter` calls. Invalid or unproved arithmetic remains unclassifiable and therefore cannot be rejected.

The proxy recomputes the same degree conversion and SDL float sine/cosine values. Thus there is no assumed libm approximation error between proxy and classifier; any non-finite value or coefficient outside `[-1,1]` fails certification. For the ideal projected-then-rotated rectangle, the output half extents are

`ex = (|cos|*width*scaleX + |sin|*height*scaleY)/2`

`ey = (|sin|*width*scaleX + |cos|*height*scaleY)/2`.

The world proxy divides `ex` and `ey` by their matching projection scales, centers the result on the synchronized world position, expands by `2/scaleX` and `2/scaleY`, and rounds every endpoint outward with `nextafter`. This proxy depends on projection but not camera translation. The query uses the current camera coordinate and double extents `outputWidth/scaleX` and `outputHeight/scaleY`, with the same two-pixel outward envelope. It also rounds endpoints outward.

## Error enclosure

Let `u = float_epsilon/2` and `eta = float_denorm_min/2`, the round-to-nearest relative and absolute error bounds for a basic float operation. The certificate bounds exact intermediate magnitudes by `8P`: camera-relative position terms are at most `2.5P`, width/height terms at most `P`, right/bottom and center terms below `4P`, corner offsets are a half dimension plus their subtraction error, and the rotated sum plus center remains below `8P`. The checks above reject overflow-prone pre-scale ranges, non-normal scales, subnormal final rendered dimensions, and invalid trigonometry. Remaining subnormal intermediate rounding is included through `eta`.

At most 32 rounded float operations lie on any dependency path from world center/dimensions through destination, corner, rotation, and transformed output. Charging each operation the full `8P` magnitude gives a deliberately loose output-space deviation bound of `32*(8*u*P + eta)`. The two pre-scale coordinates are charged after scaling, so the implementation's documented aggregate bound is

`E = 128*float_epsilon*P + 32*eta*(1 + max(scaleX,scaleY)) + 64*double_epsilon*P`.

At `P=16384`, the float term is `0.25` output pixels. Even with the largest finite float scale, the absolute subnormal term is below `0.00001` pixels; the double term is below `0.000001` pixels. The classifier's own rounding allowance is at most `8*epsilon*(8P) = 0.125` pixels under the certified intermediate bound. The full required margin is therefore below `1 + 0.25 + 0.125 + 0.00002 = 1.375` pixels, inside the implemented two-pixel envelope. This leaves more than `0.625` pixel of slack; the envelope is not a fitted tolerance.

For query endpoints, the two-pixel margin dominates double division/addition rounding over the same bounded output range. `nextafter` then directs the final endpoint rounding outward. If a division, endpoint, or extent is non-finite or nonpositive, the helper returns failure and selection falls back to current-linear classification.

## Independent test coverage

`tests/render_culling_geometry_test.cpp` independently constructs the current float destination expression, checks its padded classifier bounds against transformed double corners, maps those actual padded bounds back to world coordinates, and verifies the projection proxy encloses them. It checks that any currently retained sprite overlaps the independently computed view query. Inputs include fractional coordinates and boundary contact, unequal output scales, elongated shapes, rotations near quadrant boundaries, 2,000 deterministic ordinary cases, a large shared camera/sprite offset, the exact `P` boundary, the first float beyond `P`, extreme finite values, overflow, and subnormal scales/dimensions. At least the ordinary cases must certify; an implementation that always falls back fails.

The certificate is intentionally sufficient rather than maximal. Values outside it stay candidates or select the existing current-linear path. Widening the domain or reducing the envelope requires rerunning the independent oracle and updating this derivation first.

## Focused execution

Built `platformator_render_culling_geometry_test` in Release with work probes enabled using the options in `quickstart.md`, then ran:

```sh
cmake --build build/009-incremental-validation --target platformator_render_culling_geometry_test --parallel 4
ctest --test-dir build/009-incremental-validation --output-on-failure -R '^platformator_render_culling_geometry_test$'
```

The final compile and focused CTest passed (1/1). The first test compile caught a missing forward declaration in the new regression code; that was corrected before the passing run. No timing capture was run.

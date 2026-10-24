from platformator_ui.services.asset_paths import canonicalize_asset_path


def test_canonicalize_asset_path_matches_engine_contract() -> None:
    assert canonicalize_asset_path(r"assets\textures\MiXeD.PNG") == "assets/textures/MiXeD.PNG"
    assert canonicalize_asset_path("player/idle_0.png") is None
    assert canonicalize_asset_path("assets/scenes/../textures/player.png") is None
    assert canonicalize_asset_path("./assets/textures/player.png") is None
    assert canonicalize_asset_path("ASSETS/textures/player.png") is None
    assert canonicalize_asset_path("assets") is None
    assert canonicalize_asset_path("assets/") is None
    assert canonicalize_asset_path("assets/.") is None
    assert canonicalize_asset_path("") == ""
    assert canonicalize_asset_path("../ball.png") is None
    assert canonicalize_asset_path("/tmp/not_in_assets/default_cube.obj") is None

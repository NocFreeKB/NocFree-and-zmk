<!-- SPDX-License-Identifier: MIT -->

# Building

Two images are produced, one per half:

| Board target | Artifact |
|---|---|
| `nocfree_and_left/nrf52833/zmk` | left / split central |
| `nocfree_and_right/nrf52833/zmk` | right / split peripheral |

Every dependency is public and pinned in [`config/west.yml`](../config/west.yml)
to an exact ZMK commit, which in turn pins Zephyr. A clean checkout of this
repository is sufficient to reproduce both images.

## GitHub Actions

`.github/workflows/build.yml` runs the repository tests, then ZMK's standard
`build-user-config` workflow over [`build.yaml`](../build.yaml) — pinned to the
same ZMK commit as `config/west.yml` — and separately rebuilds both halves in
the ZMK build container to run the artifact checks (flash bounds, roles,
linkage) against the images. Fork or clone this repository, push, and collect
the two `.uf2` files from the run artifacts.

## Locally, with Docker

```sh
./scripts/build-local.sh
```

The west workspace is created in a sibling directory (`../nocfree-and-zmk-build`
by default), so nothing inside this repository is modified. Pass a different
path as the first argument to relocate it.

Artifacts land at:

```
<workspace>/build/left/zephyr/zmk.uf2
<workspace>/build/right/zephyr/zmk.uf2
```

## Locally, with an existing ZMK setup

```sh
mkdir -p ~/nocfree-ws/config
cp -R config/. ~/nocfree-ws/config/
cd ~/nocfree-ws
west init -l config
west update
west zephyr-export

west build -p -s zmk/app -d build/left  -b nocfree_and_left/nrf52833/zmk  \
    -- -DZMK_CONFIG=$PWD/config -DZMK_EXTRA_MODULES=/path/to/this/repo
west build -p -s zmk/app -d build/right -b nocfree_and_right/nrf52833/zmk \
    -- -DZMK_CONFIG=$PWD/config -DZMK_EXTRA_MODULES=/path/to/this/repo
```

Keep the workspace outside this repository: `west update` writes `zephyr/`,
`modules/` and `zmk/` into the workspace root, and this repository already has a
`zephyr/` directory holding its module definition.

## Customising the keymap

The default keymap is
[`boards/nocfree/nocfree_and/nocfree_and.keymap`](../boards/nocfree/nocfree_and/nocfree_and.keymap).
To change it without editing the board, add `config/nocfree_and.keymap` and it
will take precedence, following ZMK's normal user-config rules.

ZMK Studio is not enabled; see [limitations.md](limitations.md).

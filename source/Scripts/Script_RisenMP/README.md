# RisenMP prototype v0.1

This is the first in-game plugin/networking milestone for the Risen 1 multiplayer project.

## What is implemented

- Risen script plugin entry point: `ScriptInit()`.
- Console command: `risenmp`.
- Host-based UDP session model.
- Host accepts up to three remote peers (four players total including host).
- IPv4 LAN join by address and UDP port.
- Protocol magic/version and strict datagram-size checks.
- Hello/Welcome/Reject/Disconnect and Ping/Pong packet types.
- Network I/O runs on a worker thread; no Genome engine objects are touched from that thread.

## Build

The project follows the RisenSDK Visual C++ 2005 project format and imports the SDK script property sheets:

- `source/RisenScriptsDebug.vsprops`
- `source/RisenScriptsRelease.vsprops`

Build `Script_RisenMP.vcproj` as Win32. The linker also requires `ws2_32.lib`.

Output DLL must be named:

`bin/scripts/Script_RisenMP.dll`

The game loads script libraries using the `Script_*.dll` naming convention described by RisenSDK.

## In-game usage

Open the Risen console and run:

```text
risenmp host 28000
```

On another PC in the same LAN:

```text
risenmp join 192.168.1.10 28000
```

Check the session:

```text
risenmp status
```

Stop networking:

```text
risenmp stop
```

Allow inbound UDP traffic on the selected port in Windows Firewall. Both clients must use the same Risen version and compatible game data.

## Explicit limitations of v0.1

This is a networking/session prototype, not yet a playable co-op release.

- No remote character is spawned in the world.
- No player transform is sampled from the game loop or applied to an entity.
- `PlayerSnapshotPayload` is reserved in the protocol but is not transmitted.
- No animation, combat, NPC, inventory, quest, save-game, or world-state replication.
- LAN IPv4 only. No Internet NAT traversal, STUN, hole punching, or relay.
- The host currently tracks peer endpoints and handshake state; it does not yet arbitrate gameplay.

The next implementation step is the main-thread game adapter: safely obtain the local player transform, schedule network snapshots from a verified game update callback, create a remote entity from a valid player template, and interpolate received transforms. The SDK exposes candidate world/entity APIs, but those lifecycle calls must be validated against the running Risen binary before they are used to create or destroy entities.

## Compatibility and licensing

The SDK identifies Risen 1 patch 1.10 and Visual C++ 2005 SP1 as its target. This project is Win32-only and uses the SDK's script import property sheets.

RisenSDK is licensed CC BY-NC-SA 3.0. Review its attribution, non-commercial, and share-alike terms before distributing a combined derivative.

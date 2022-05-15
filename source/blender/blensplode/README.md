# blensplode

This command line application can be used to explode (disassemble) a Blender file into its constituent datablocks and implode (re-assemble) it.

Its main objective is to facilitate versioning of Blender files using version control systems like git.

> NOTE: Still under development. Not ready for production usage.

## Format

The high-level representation of the exploded Blender file is described by a JSON file as follows:

```jsonc
{
  "header": {
    "version": 300,
    "endianness": "little", // Tool preserves architecture of file used to originally save it, so that the original Blender file can be reconstructed perfectly
    "pointer_size": 8
  },
  "datablocks": [
    {
      // BHead details
      "code": "GLOB",
      "old": "0x7ffdda7d21a0",
      "SDNAnr": 308,
      "type": "FileGlobal",
      "file": "fileglobal_16328c40",  // datablock contents stored as binary file
      "data": "yGCUPth/AABI1KF22H8AAAAAAAAAAAAAAAAAAAAAAAA="  // datablock contents stored as base64 (datablocks <=100 bytes)
    },
  ]
}
```

## CLI description

```
Blender format exploder/imploder
Usage:
  blensplode [OPTION...] <input-file>

  -h, --help     produce help message
  -i, --implode  implode file instead of explode
```

## CLI examples

### To disassemble `cube_diorama.blend` into `cube_diorama.json`

```
blensplode cube_diorama.blend
```

### To re-assemble `cube_diorama.json` into `cube_diorama.blend`

```
blensplode -i cube_diorama.json
```

## TODO

* Need to create a canonical memory address mapping to reduce irrelevant deltas
* Replace binary blobs with more human readable representations of datablocks where possible (text, image)
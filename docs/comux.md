**CoMux** is short for **Co**nnection **Mu**ltiple**x**ing. It's a special file format I implemented as part of this project to specify the content of multiple socket connections in a single file.

# Comux File Layout

Each comux file is formatted like so:

```
+---------------------------------------------------+
|   MAGIC    |  VERSION   | NUM_CONNS  | NUM_CHUNKS |
+------------+------------+------------+------------+
|   C1_ID    |   C1_LEN   |  C1_SCHED  |  C1_FLAGS  |
+------------+------------+------------+------------+
|                      C1_DATA                      |
+------------+------------+------------+------------+
|   C2_ID    |   C2_LEN   |  C2_SCHED  |  C2_FLAGS  |
+------------+------------+------------+------------+
|                      C2_DATA                      |
+------------+------------+------------+------------+
|                        ...                        |
+------------+------------+------------+------------+
|   CN_ID    |   CN_LEN   |  CN_SCHED  |  CN_FLAGS  |
+------------+------------+------------+------------+
|                      CN_DATA                      |
+---------------------------------------------------+
```

# Design

Below are notes and discussion about the layout of this file: what the fields mean, and why they're present.

### The `magic` Field

The `magic` is a simple chunk of bytes that's used to identify a file as a comux file. It's a good sanity check when opening a comux file and parsing it. It's comparable to the `ELF` bytes seen at the front of an ELF binary.

Presently, the magic is 8 bytes long: `comux!!!` (the word "comux" followed by three exclamation points).

### The `version` Field

This field marks the comux version number the file is formatted in. Presently the only version number is zero, but this field was included for extensibility and backwards-compatibiilty, should this file format be adopted and modified by others.

### The `num_conns` Field

This is an unsigned integer specfiyng the number of concurrent connections to be spawned when communicating with the target server. All of the chunks listed in the file have a connection ID between `0` and `NUM_CONNS`.

### The `num_chunks` Field

This is an unsigned integer specifying the number of chunk-header and chunk-data pairs specified in the file after the main header.

## Main Header

The main comux header defines a few fields and is immediately followed by the first chunk header.

## Chunk Headers

A chunk header marks the beginning of a new chunk segment in the comux file. Each chunk represents a set of bytes to be sent to one of the `NUM_CONNS` connections. It contains the `id`, `length`, `schedule`, and `flags` fields, and is immediately followed by that chunk's data.

### The `id` Field

The `id` field is a simple unsigned integer that specifies *which* connection this data is assigned to. It can vary between `0` and `NUM_CONNS` (specified in the main comux header).

### The `length` Field

Each chunk header has a `length` field. This is used to specify the length of the data following the header. A reader of this comux file can use this field to quickly seek from one chunk header to the next.

#### **Why not include an offset list, rather than lengths for each chunk header?**

An alternative design approach would be to place a list of chunk header offsets at the front of the file, immediately after the comux header. This would make it easy for a parser to immediately understand *exactly* where each chunk header begins in the file.

However, this design choice creates more overhead: the reader of a comux file will need to create some data structure to hold all of these offsets. With the chosen approach, this isn't necessary - the reader simply needs to walk through the file, parsing each chunk length as it goes. If it's truly needed, the reader could still create a table of offsets after making a single pass through the file.

### The `schedule` Field

The `schedule` field defines a scheduling value for each chunk. This value is used to decide *when* the chunk of data is sent across the wire to the target server. Chunks with *lower* scheduling values are sent first. Take this example:

```
COMUX FILE: num_conns=3, num_chunks=6
CHUNK 0: conn_id=0, sched=1
CHUNK 1: conn_id=2, sched=0
CHUNK 2: conn_id=1, sched=3
CHUNK 3: conn_id=1, sched=4
CHUNK 4: conn_id=2, sched=5
CHUNK 5: conn_id=0, sched=2
```

The chunks would be sent in the following order:

1. Chunk 1 --> connection 2
2. Chunk 0 --> connection 0
3. Chunk 5 --> connection 0
4. Chunk 2 --> connection 1
5. Chunk 3 --> connection 1
6. Chunk 4 --> connection 2


#### **Why not just follow the order in the file?**

The purpose of having this scheduling field for each chunk is to allow the mutator (the other half of this project) to fuzz the order in which chunks are sent, simply by modifying a chunks scheduling field.

### The `flags` Field

This is used to toggle various switches to tell the `LD_PRELOAD` library *how* to treat this connection.

It's entirely possible more flags will be implemented in the future, so this field also exists for extensibility purposes.

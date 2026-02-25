# fabricaai stm32 libcanopen
## Features
- Create specific CanOpen frames: NMT, SDO
- NMT state machine management
- Automatic SDO processing and error management
- TODO: node PDO if needed

## Usage
### NMT
- Implement `CanOpen_Nmt_InitCompleteCallback()`
    - The implementation of this function should also send a heartbeat message
- Implement `CanOpen_Nmt_OnResetCallback()`
    - The implementation of this function should reinitialize the internal system.
- On receiving an NMT frame, call `CanOpen_Nmt_ProcessFrame()` to process the frame.
    - This calls `CanOpen_Nmt_OnResetCallback()` if the frame is a reset frame
- To signal a change in NMT state, call `CanOpen_Nmt_SetState()`
    - If we're changing from start state/reset state to preoperational, `CanOpen_Nmt_InitCompleteCallback()`
      will be called

### NMT Demo
```c
void Update() {
    CanOpen_Frame frame;

    // Assume getting an NMT frame
    DequeueRx(&frame);

    // Processes the frame, internal NMT state updated automatically
    CanOpen_Nmt_ProcessFrame(&frame);
}

void CanOpen_Nmt_InitCompleteCallback() {
    // according to the spec we need to transmit a frame on init complete
    CanOpen_Frame frame = CanOpen_Heartbeat_CreatePreOpStateFrame(Can_GetId());
    EnqueueTx(&frame);
}

void CanOpen_Nmt_OnResetCallback() {
    // Soft reinit logic goes here
    Deinit();
    Init();
}

```

### SDO
- Populate the object dictionary by calling
`ObjDict_CreateEntry(index, num_subindices, permissions, size, read_callback, write_callback)`
    - Arguments:
        - `index`: `uint16_t` - 2 bytes index
        - `num_subindices`: `uint8_t` - number of subindices of this index
        - `permissions`: `CanOpen_ObjEntryPermissions` -  access permissions for this object, RO, WO or RW
        - `size`: `ObjEntrySizeBytes` - number of bytes of this object (up to 4)
        - `read_callback`: Function pointer callback for when a valid SDO read frame for this object is received
            - Arguments:
                - `ObjData* data`: pointer to object data to write to.
                - `uint8_t` subindex: subindex of object
            - Return:
                - `uint32_t` : error code if any. 0 if no error
        - `write_callback`: Funcion pointer callback for when a valid SDO write frame for this object is received
            - Arguments:
                - `uint8_t subindex`: subindex of object
                - `ObjDict_Data`: data to write
            - Return:
                - `uint32_t` : error code if any. 0 if no error
    - Return: `size_t` current size of obj dict, size 0 if errored adding the obj dict

### SDO Demo
```c
static uint16_t our_data[3] = {1, 2, 3};

uint32_t ReadCallback(ObjDict_Data* data, const uint8_t subindex) {
    // this will get the data from our array at the subindex position
    data->u16 = our_data[subindex - 1];
    return 0;
}

void Init() {
    ObjDict_CreateEntry(0x6010, 3, OBJENTRY_PERMIT_R, OBJENTRY_SIZE_SHORT, ReadCallback, NULL);
}

void Update() {
    CanOpen_Frame rx_frame, tx_frame;

    // Assume getting a SDO frame
    DequeueRx(&rx_frame);

    // tx_frame is the frame to transmit after processing the sdo frame
    // ObjDict_HandleSdo() will call Read/WriteCallback to actually store/fetch the data
    tx_frame = ObjDict_HandleSdo(&rx_frame);

}
```
### TODO: EMCY

### TODO: PDO if we need

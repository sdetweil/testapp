repro

build and upload and monitor

```
starting advertising
00000001-27b9-42f0-82aa-2e951747bbf9
after starting advertisingadvertising
end of setup
end of changed check
toggling uuid
done toggling, active=00000011-27b9-42f0-82aa-2e951747bbf9
```
every 15 seconds it will toggle between the two active servivces

use nrfConnect, connect
and write '1' (text mode) to the 9A64 characteristic, 
  this will start the OTA (update over the air, via ble) service in addition to the two alredy created
```
connected
write characteristic=0x9a64
OTA STart requested
restart advertising after resetup
done startOTAService
```

now, nrfConnect is still connected..
disconnect (from prior services, no longer in the advertised uuid list)

crash 
```
assert failed: ble_svc_gap_init ble_svc_gap.c:302 (rc == 0)
``` 

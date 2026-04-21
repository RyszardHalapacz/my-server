## Logging IDs

We use `.def` files and enums to avoid string duplication in logs.

### Files
- `log_classes.def` → unique class names.  
- `log_ids.def` → all pairs (class + method).  

### Enums
From these files we generate:
- `LogClassId` → enum with all classes.  
- `MethodId` → enum with all class+method pairs.  

Example:
```cpp
LogClassId::Server
MethodId::Server_AddEvent

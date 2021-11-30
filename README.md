## Proof of concept for drp-python

### To compile:

The sysv_ipc module needs to be installed locally:


```
pip install ---user sysv_ipc
```

Then:

```
conda activate ps-4.5.7
sh compile.sh
```


### To run:

```
conda activate ps-4.5.7
./drp_test ./drp_python.py
```

### Notes:


* To list "leftover" IPC structures

  ```
  ipcs
  ```

* To remove all user-owned "leftover" IPC structures"

  ```
  ipcrm -a
  ```


# Enrollment receipt postcheck

After the compile helper has settled, pass its strict receipt's observed driver
`root_exit` as the sole decimal argument to the fixed postcheck under the separate
strict helper. A successful exit emits one JSON object with schema
`enrollment-postcheck-v1`, result `VALIDATED`, the validated output records, and
receipt handle counts. Accept it only after postcheck settlement and compare the
actual output inventory sizes and hashes with those records. A nonzero exit or
missing success object is a failure. The postcheck does not launch the compiler.

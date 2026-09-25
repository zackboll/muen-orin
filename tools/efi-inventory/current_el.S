.text
.globl probe_current_el
probe_current_el:
    mrs x0, CurrentEL
    ret

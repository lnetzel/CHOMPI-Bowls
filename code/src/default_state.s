.section .rodata.default_state,"a",%progbits
.balign 4
.global default_state_json
.type default_state_json, %object
default_state_json:
.incbin "../state examples/default_pelog_selisir_inspired.json"
.global default_state_json_end
default_state_json_end:
.size default_state_json, default_state_json_end - default_state_json

#include <stdio.h>
#include "miio.h"

static void print_depth_shift(int depth)
{
	int j;
	for (j=0; j < depth; j++) {
		printf(" ");
	}
}

static void process_value(json_value* value, int depth);

static void process_object(json_value* value, int depth)
{
	int length, x;
	if (value == NULL) {
		return;
	}
	length = value->u.object.length;
	for (x = 0; x < length; x++) {
		print_depth_shift(depth);
		printf("object[%d].name = %s\n", x, value->u.object.values[x].name);
		process_value(value->u.object.values[x].value, depth+1);
	}
}

static void process_array(json_value* value, int depth)
{
	int length, x;
	if (value == NULL) {
		return;
	}
	length = value->u.array.length;
	printf("array\n");
	for (x = 0; x < length; x++) {
		process_value(value->u.array.values[x], depth);
	}
}

static void process_value(json_value* value, int depth)
{
	if (value == NULL) {
		return;
	}
	if (value->type != json_object) {
		print_depth_shift(depth);
	}
	switch (value->type) {
		case json_none:
			printf("none\n");
			break;
		case json_object:
			process_object(value, depth+1);
			break;
		case json_array:
			process_array(value, depth+1);
			break;
		case json_integer:
			printf("int: %10" PRId64 "\n", value->u.integer);
			break;
		case json_double:
			printf("double: %f\n", value->u.dbl);
			break;
		case json_string:
			printf("string: %s\n", value->u.string.ptr);
			break;
		case json_boolean:
			printf("bool: %d\n", value->u.boolean);
			break;
		case json_null:
			printf("NULL\n");
			break;
	}
}

static void
perform_cmd (char* cmd, char* opt)
{
	json_value* value = miio_cmd (cmd, opt);
	if (value) {
		process_value(value, 0);
		json_value_free (value);
	}
}

int main (int argc, char** argv)
{
	if (miio_init () != 0) { return -1; }

	perform_cmd ("get_status", "");
	perform_cmd ("get_consumable", "");
	perform_cmd ("app_stop", "");
	//perform_cmd ("get_clean_summary", "");

#if 0
	perform_cmd ("set_custom_mode", "\"params\": [70]");
	perform_cmd ("get_custom_mode", "");
	perform_cmd ("find_me", "");
	perform_cmd ("app_start", "");
	perform_cmd ("app_pause", "");
	perform_cmd ("app_stop", "");
	perform_cmd ("app_stop", ""); perform_cmd (fd, "app_charge", "");
#endif

	miio_cleanup ();
	return 0;
}

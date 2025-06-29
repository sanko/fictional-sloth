#ifndef DOUBLE_TAP_H
#define DOUBLE_TAP_H

#include <stdbool.h> // For bool type
#include <stdio.h>   // For printf, fprintf, snprintf, va_list, va_start, va_end
#include <stdlib.h>  // For malloc, free, exit, strdup, qsort
#include <stdarg.h>  // For variadic functions
#include <string.h>  // For strcmp, strlen

#include <math.h> // For fabs (for floating-point comparisons)

// Platform-specific headers and TTY detection
#ifdef _WIN32
#include <windows.h> // For console API functions
#include <io.h>      // For _isatty, _fileno
// Map POSIX names to Windows equivalents for consistency within the framework
#define isatty _isatty
#define fileno _fileno
#else
// POSIX systems
#include <unistd.h> // For isatty, fileno
#endif

// Define FFI_TESTING to 1 before including this header to enable the TAP framework macros
// If FFI_TESTING is 0 or undefined, all macros become no-ops.
#if FFI_TESTING

// --- Configuration Defines ---
#define double_tap_MAX_NESTING 10           // Maximum subtest nesting depth
#define double_tap_EPSILON 1e-9             // Epsilon for floating-point comparisons
#define NO_PLAN -1                          // Sentinel value for unknown test plan
#define DOUBLE_TAP_MAX_DESCRIPTION_LEN 2048 // Max length for test descriptions for internal buffer safety

// ANSI Color Codes
#define _DT_COLOR_RED "\033[31m"
#define _DT_COLOR_GREEN "\033[32m"
#define _DT_COLOR_RESET "\033[0m"

// --- Internal Global State Context ---
/**
 * @brief Structure to hold all static global state variables of the Double TAP framework.
 * This encapsulates state to avoid issues with static variable visibility across complex
 * compilation units or very strict compilers.
 */
typedef struct
{
    int local_test_count; // Current test count within active (sub)test
    int overall_failures; // Total count of top-level failures (tests or subtests)

    // Subtest management stack
    int test_count_stack[double_tap_MAX_NESTING];      // Saves local_test_count of parent
    bool subtest_failed_stack[double_tap_MAX_NESTING]; // Tracks if a subtest failed
    // New: Tracks if a subtest (or any of its children) had an unexpected TODO pass
    bool subtest_has_unexpected_todo_pass[double_tap_MAX_NESTING];
    char *subtest_desc_stack[double_tap_MAX_NESTING]; // Stores subtest descriptions
    volatile int stack_idx;                           // Current nesting depth (0 for top-level) - marked volatile for safety
    int explicit_plan_values[double_tap_MAX_NESTING]; // Explicit plan value for each nesting level

    int test_plan; // Expected number of top-level tests (for final check)

    // Dynamic array for failed test numbers (only for top-level failures)
    int *failed_test_numbers_list;
    size_t failed_tests_count;
    size_t failed_tests_capacity;

    // Dynamic array for unexpectedly passing TODO tests (top-level test numbers)
    int *unexpected_todo_passes_list;
    size_t unexpected_todo_passes_count;
    size_t unexpected_todo_passes_capacity;

    bool tap_version_printed; // Flag to ensure TAP version line is printed only once
    bool skip_all_active;     // Flag to indicate if skip_all was invoked (for main loop check)
    bool initialized;         // Flag to ensure one-time initialization

    bool is_stdout_tty; // True if stdout is a TTY
    bool enable_colors; // True if terminal colors should be used
} __double_tap_context_t;

// Initialize the single instance of the global state context
static __double_tap_context_t __double_tap_ctx = {
    .local_test_count = 0,
    .overall_failures = 0,
    .stack_idx = 0,
    .test_plan = NO_PLAN,
    .failed_test_numbers_list = NULL,
    .failed_tests_count = 0,
    .failed_tests_capacity = 0,
    .unexpected_todo_passes_list = NULL,
    .unexpected_todo_passes_count = 0,
    .unexpected_todo_passes_capacity = 0,
    .tap_version_printed = false,
    .skip_all_active = false, // Initialize skip_all_active to false
    .initialized = false,     // Mark as not yet initialized
    // Initialize arrays with 0, will be properly set by __double_tap_init_context
    .test_count_stack = {0},
    .subtest_failed_stack = {false},
    .subtest_has_unexpected_todo_pass = {false},
    .subtest_desc_stack = {NULL},
    .explicit_plan_values = {0},
    .is_stdout_tty = false, // Will be set during init
    .enable_colors = false  // Will be set during init based on is_stdout_tty
};

#ifdef _WIN32
/**
 * @brief Enables ANSI escape code processing for the console output on Windows.
 * This is necessary for colors to work in cmd.exe or PowerShell (though newer PowerShell
 * versions might handle it by default, it's safer to ensure).
 */
static inline void __double_tap_enable_ansi_colors_on_windows()
{
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut == INVALID_HANDLE_VALUE)
    {
        return; // Cannot get handle
    }

    DWORD dwMode = 0;
    if (!GetConsoleMode(hOut, &dwMode))
    {
        return; // Cannot get console mode
    }

    dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    if (!SetConsoleMode(hOut, dwMode))
    {
        // Failed to set console mode, possibly an older Windows version
        // Colors will simply not appear. No need to bail out.
    }
}
#endif // _WIN32

/**
 * @brief Performs one-time initialization of the global context.
 * This is called by the first `plan()`, `ok()`, `subtest()`, etc. macro.
 */
static inline void __double_tap_init_context()
{
    if (!__double_tap_ctx.initialized)
    {
        // Initialize explicit_plan_values with NO_PLAN
        for (int i = 0; i < double_tap_MAX_NESTING; ++i)
        {
            __double_tap_ctx.explicit_plan_values[i] = NO_PLAN;
            __double_tap_ctx.subtest_has_unexpected_todo_pass[i] = false; // Ensure all are false initially
        }
        // Detect if stdout is a TTY
        __double_tap_ctx.is_stdout_tty = (bool)isatty(fileno(stdout));
        // Enable colors by default if it's a TTY
        __double_tap_ctx.enable_colors = __double_tap_ctx.is_stdout_tty;

#ifdef _WIN32
        // On Windows, also attempt to enable ANSI escape code processing
        if (__double_tap_ctx.is_stdout_tty)
        {
            __double_tap_enable_ansi_colors_on_windows();
        }
#endif

        __double_tap_ctx.initialized = true;
    }
}

/**
 * @brief Enables or disables terminal color output.
 * If output is not to a TTY, colors will not be used regardless of this setting.
 * @param enable Boolean, true to enable colors, false to disable.
 */
static inline void double_tap_set_color_output(bool enable)
{
    __double_tap_init_context(); // Ensure context is initialized before setting
    __double_tap_ctx.enable_colors = enable && __double_tap_ctx.is_stdout_tty;
}

// --- Helper Functions (Static and Inline) ---

/**
 * @brief Prints spaces for indentation based on current nesting depth to a specified stream.
 * @param stream The file stream to print to (stdout or stderr).
 * @param depth The current indentation depth.
 */
static inline void __double_tap_print_indent(FILE *stream, int depth)
{
    for (int i = 0; i < depth; ++i)
    {
        fprintf(stream, "    "); // 4 spaces per level
    }
}

/**
 * @brief Prints a diagnostic message to stderr, prefixed with '#' and indented.
 * @param depth The indentation depth for the message.
 * @param fmt Format string for the message.
 * @param ... Arguments for the format string.
 */
static inline void __double_tap_diag_printf_internal(int depth, const char *fmt, ...)
{
    __double_tap_init_context(); // Ensure context is initialized
    __double_tap_print_indent(stderr, depth);
    fprintf(stderr, "# ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    fprintf(stderr, "\n");
}

/**
 * @brief Prints a general note message to stdout, prefixed with '#' and indented.
 * @param depth The indentation depth for the message.
 * @param fmt Format string for the message.
 * @param ... Arguments for the format string.
 */
static inline void __double_tap_note_printf_internal(int depth, const char *fmt, ...)
{
    __double_tap_init_context(); // Ensure context is initialized
    __double_tap_print_indent(stdout, depth);
    printf("# ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    printf("\n");
}

/**
 * @brief Formats a string and prints it to the stream, escaping '#' and '\' characters.
 * This function handles its own va_list copies.
 * @param stream The file stream to print to.
 * @param fmt Format string for the message.
 * @param args The va_list for the format string arguments.
 */
static inline void __double_tap_vprint_escaped_string(FILE *stream, const char *fmt, va_list args)
{
    va_list args_copy_for_len;
    va_copy(args_copy_for_len, args);
    int len_needed_un_escaped = vsnprintf(NULL, 0, fmt, args_copy_for_len);
    va_end(args_copy_for_len);

    if (len_needed_un_escaped < 0)
    {
        fprintf(stream, "(Error formatting string)");
        return;
    }

    // Allocate a buffer for the formatted string (before escaping)
    char *temp_un_escaped_buf = (char *)malloc((size_t)len_needed_un_escaped + 1);
    if (!temp_un_escaped_buf)
    {
        fprintf(stream, "(Out of memory for string)");
        return;
    }

    va_list args_copy_for_format;
    va_copy(args_copy_for_format, args);
    vsnprintf(temp_un_escaped_buf, (size_t)len_needed_un_escaped + 1, fmt, args_copy_for_format);
    va_end(args_copy_for_format);

    // Now, build the escaped string
    // Max size needed: len_needed_un_escaped (original chars) + len_needed_un_escaped (for potential backslashes) + 1 (null terminator)
    size_t escaped_buf_max_size = (size_t)len_needed_un_escaped * 2 + 1;
    char *escaped_output_buf = (char *)malloc(escaped_buf_max_size);
    if (!escaped_output_buf)
    {
        fprintf(stream, "(Out of memory for escaped string)");
        free(temp_un_escaped_buf);
        return;
    }

    size_t current_escaped_len = 0;
    for (char *p = temp_un_escaped_buf; *p != '\0'; ++p)
    {
        if (*p == '#' || *p == '\\')
        { // Escape both # and \
            // Ensure there's space for '\' + char + null terminator
            if (current_escaped_len + 2 < escaped_buf_max_size)
            {
                escaped_output_buf[current_escaped_len++] = '\\';
                escaped_output_buf[current_escaped_len++] = *p;
            }
            else
            {
                // Buffer too small, truncate and break
                break;
            }
        }
        else
        {
            // Ensure there's space for char + null terminator
            if (current_escaped_len + 1 < escaped_buf_max_size)
            {
                escaped_output_buf[current_escaped_len++] = *p;
            }
            else
            {
                // Buffer too small, truncate and break
                break;
            }
        }
    }
    escaped_output_buf[current_escaped_len] = '\0'; // Null-terminate the escaped string

    fprintf(stream, "%s", escaped_output_buf);

    free(temp_un_escaped_buf);
    free(escaped_output_buf);
}

/**
 * @brief Prints the basic "ok N - description" or "not ok N - description" part of a test line.
 * This helper does NOT append a newline and does NOT modify global state.
 * @param condition The test result (true for ok, false for not ok).
 * @param depth The current indentation depth.
 * @param test_number The local test number.
 * @param fmt Format string for the description.
 * @param ... Arguments for the description format string.
 */
static inline void _double_tap_print_test_line_base(bool condition, int depth, int test_number, const char *fmt, ...)
{
    __double_tap_init_context(); // Ensure context is initialized
    __double_tap_print_indent(stdout, depth);

    if (__double_tap_ctx.enable_colors)
    {
        printf("%s", condition ? _DT_COLOR_GREEN : _DT_COLOR_RED);
    }

    if (condition)
    {
        printf("ok %d", test_number);
    }
    else
    {
        printf("not ok %d", test_number);
    }

    if (__double_tap_ctx.enable_colors)
    {
        printf("%s", _DT_COLOR_RESET); // Reset color after "ok N" or "not ok N"
    }

    printf(" - "); // Separator for description
    va_list args;
    va_start(args, fmt);
    __double_tap_vprint_escaped_string(stdout, fmt, args); // Print escaped description
    va_end(args);                                          // Clean up the va_list
    // No newline here, allows appending directives like # SKIP or # TODO
}

// --- Canonical Value Union for Type-Aware Comparisons ---

/**
 * @brief Union to hold canonical representations of different C types for comparison.
 */
typedef union
{
    long long ll_val;      // For all integer types (char, short, int, long, long long, unsigned variants)
    double d_val;          // For float and double
    const char *s_ptr_val; // For string literals (const char*)
    const void *p_val;     // For generic pointers (const void*)
    __int128_t i128;
} __double_tap_canonical_val;

/**
 * @brief Enum for different type kinds used in comparison macros.
 */
typedef enum
{
    double_tap_TYPE_UNKNOWN,
    double_tap_TYPE_CHAR,
    double_tap_TYPE_INT,
    double_tap_TYPE_FLOAT,
    double_tap_TYPE_DOUBLE,
    double_tap_TYPE_STRING,
    double_tap_TYPE_PTR,
    double_tap_TYPE_INT128
} __double_tap_type_kind;

/**
 * @brief Helper function to print a value from the canonical union based on its type to a string buffer for YAML output.
 * Handles YAML-specific formatting (e.g., '~' for null, quoted strings).
 * @param buffer The character buffer to print into.
 * @param buf_size The size of the buffer.
 * @param type_token The type kind of the value.
 * @param val_union_ptr Pointer to the union containing the value.
 * @return The number of characters printed (excluding null terminator), or a negative value on error.
 */
static inline int __double_tap_sprint_val_to_yaml_string(char *buffer, size_t buf_size, int type_token, const __double_tap_canonical_val *val_union_ptr)
{
    if (!val_union_ptr)
    {
        return snprintf(buffer, buf_size, "~"); // YAML null
    }

    switch (type_token)
    {
    case double_tap_TYPE_CHAR:
        // For YAML, char is often represented as a single character string or integer
        // Printing as a quoted string is generally safe and clear.
        return snprintf(buffer, buf_size, "'%c'", (char)val_union_ptr->ll_val);
    case double_tap_TYPE_INT:
        return snprintf(buffer, buf_size, "%lld", val_union_ptr->ll_val);
    case double_tap_TYPE_FLOAT: // Fallthrough
    case double_tap_TYPE_DOUBLE:
        // Print floats/doubles directly. YAML will parse them.
        return snprintf(buffer, buf_size, "%f", val_union_ptr->d_val);
    case double_tap_TYPE_STRING:
        if (val_union_ptr->s_ptr_val == NULL)
        {
            return snprintf(buffer, buf_size, "~"); // YAML null for string pointers
        }
        // Safely quote strings for YAML, especially since they can contain spaces/special chars.
        return snprintf(buffer, buf_size, "\"%s\"", val_union_ptr->s_ptr_val);
    case double_tap_TYPE_PTR:
        if (val_union_ptr->p_val == NULL)
        {
            return snprintf(buffer, buf_size, "~"); // YAML null for pointers
        }
        return snprintf(buffer, buf_size, "%p", val_union_ptr->p_val);
    default:
        return snprintf(buffer, buf_size, "(unknown_type)");
    }
}

/**
 * @brief This is an internal helper function for BAIL_OUT.
 * It's declared static inline so each translation unit gets its own copy,
 * ensuring the linker doesn't complain about undefined references.
 * @param fmt Format string for the bail out message.
 * @param ... Arguments for the message format string.
 */
static inline __attribute__((noreturn)) void __double_tap_bail_out_internal(const char *fmt, ...)
{
    fprintf(stdout, "Bail out! ");
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    fprintf(stdout, "\n");
    exit(255);
}

/**
 * @brief Comparison function for qsort to sort integers in ascending order.
 */
static int __double_tap_compare_ints(const void *a, const void *b)
{
    return (*(const int *)a - *(const int *)b);
}

/**
 * @brief Adds the test number of a failed top-level test to the global list of true failures,
 * ensuring uniqueness. This is ONLY for non-TODO tests or overall plan mismatches.
 * @param test_number The test number of the failed test.
 */
static inline void __double_tap_add_overall_failed_test_number(int test_number)
{
    // Check if the test number is already in the list
    for (size_t i = 0; i < __double_tap_ctx.failed_tests_count; ++i)
    {
        if (__double_tap_ctx.failed_test_numbers_list[i] == test_number)
        {
            return; // Test number already recorded, do nothing
        }
    }

    // If not found, add it
    if (__double_tap_ctx.failed_tests_count >= __double_tap_ctx.failed_tests_capacity)
    {
        size_t new_capacity = (__double_tap_ctx.failed_tests_capacity == 0) ? 4 : __double_tap_ctx.failed_tests_capacity * 2;
        int *new_list = (int *)realloc(__double_tap_ctx.failed_test_numbers_list, new_capacity * sizeof(int));
        if (!new_list)
        {
            __double_tap_bail_out_internal("Failed to reallocate memory for failed test numbers list.");
        }
        __double_tap_ctx.failed_test_numbers_list = new_list;
        __double_tap_ctx.failed_tests_capacity = new_capacity;
    }
    __double_tap_ctx.failed_test_numbers_list[__double_tap_ctx.failed_tests_count++] = test_number;
    __double_tap_ctx.overall_failures++; // Increment overall failures only when adding a unique failure
}

/**
 * @brief Adds the test number of a top-level TODO test that unexpectedly passed to a dedicated list.
 * @param test_number The test number of the unexpectedly passing TODO test.
 */
static inline void __double_tap_add_unexpected_todo_pass_number(int test_number)
{
    // Check if the test number is already in the list
    for (size_t i = 0; i < __double_tap_ctx.unexpected_todo_passes_count; ++i)
    {
        if (__double_tap_ctx.unexpected_todo_passes_list[i] == test_number)
        {
            return; // Test number already recorded, do nothing
        }
    }

    // If not found, add it
    if (__double_tap_ctx.unexpected_todo_passes_count >= __double_tap_ctx.unexpected_todo_passes_capacity)
    {
        size_t new_capacity = (__double_tap_ctx.unexpected_todo_passes_capacity == 0) ? 4 : __double_tap_ctx.unexpected_todo_passes_capacity * 2;
        int *new_list = (int *)realloc(__double_tap_ctx.unexpected_todo_passes_list, new_capacity * sizeof(int));
        if (!new_list)
        {
            __double_tap_bail_out_internal("Failed to reallocate memory for unexpected TODO passes list.");
        }
        __double_tap_ctx.unexpected_todo_passes_list = new_list;
        __double_tap_ctx.unexpected_todo_passes_capacity = new_capacity;
    }
    __double_tap_ctx.unexpected_todo_passes_list[__double_tap_ctx.unexpected_todo_passes_count++] = test_number;
}

/**
 * @brief Performs the actual comparison logic for different types.
 * @param is_equality True for equality check, false for inequality check.
 * @param type_kind The type kind of the values being compared.
 * @param actual_union_ptr Pointer to the union holding the actual value.
 * @param expected_union_ptr Pointer to the union holding the expected value.
 * @param epsilon Epsilon for floating-point comparisons.
 * @return True if the comparison condition is met, false otherwise.
 */
static inline bool __double_tap_perform_comparison(bool is_equality, int type_kind,
                                                   const __double_tap_canonical_val *actual_union_ptr,
                                                   const __double_tap_canonical_val *expected_union_ptr,
                                                   double epsilon)
{
    switch (type_kind)
    {
    case double_tap_TYPE_CHAR: // Fallthrough
    case double_tap_TYPE_INT:
    case double_tap_TYPE_INT128:
        return is_equality ? (actual_union_ptr->ll_val == expected_union_ptr->ll_val) : (actual_union_ptr->ll_val != expected_union_ptr->ll_val);
    case double_tap_TYPE_FLOAT: // Fallthrough
    case double_tap_TYPE_DOUBLE:
        return is_equality ? (fabs(actual_union_ptr->d_val - expected_union_ptr->d_val) < epsilon) : (fabs(actual_union_ptr->d_val - expected_union_ptr->d_val) >= epsilon);
    case double_tap_TYPE_STRING:
    {
        const char *actual_s = actual_union_ptr->s_ptr_val;
        const char *expected_s = expected_union_ptr->s_ptr_val;

        // Specific requirements for string NULL handling:
        // is(NULL, NULL, STRING) and is("test", NULL, STRING) should FAIL
        // is_not("test", NULL, STRING) should PASS
        if (is_equality)
        {
            // For 'is': if either is NULL, it fails, unless both are non-NULL and strcmp returns 0.
            if (actual_s == NULL || expected_s == NULL)
            {
                return false; // Comparison fails if any is NULL (as per requirement: "meaningful string comparison")
            }
            return (strcmp(actual_s, expected_s) == 0);
        }
        else
        { // is_not
            // For 'is_not': if one is NULL and other is non-NULL, it passes.
            // If both are NULL, it passes (not meaningfully equal as per requirement).
            // If both are non-NULL, then strcmp is used.
            if (actual_s == NULL && expected_s == NULL)
            {
                return true; // Both NULL means they are not "meaningfully" equal, so is_not true.
            }
            if (actual_s == NULL || expected_s == NULL)
            {
                return true; // One NULL, one not, means they are not equal, so is_not true.
            }
            return (strcmp(actual_s, expected_s) != 0);
        }
    }
    case double_tap_TYPE_PTR:
        return is_equality ? (actual_union_ptr->p_val == expected_union_ptr->p_val) : (actual_union_ptr->p_val != expected_union_ptr->p_val);
    default:
        //note("Failed to compare unknown types: %c / %d", type_kind, type_kind);
        return false; // Unknown type, comparison fails
    }
}

/**
 * @brief Core macro that performs the comparison after values are canonicalized.
 * This helper is now called by the user-facing 'is_X' and 'is_not_X' macros
 * after they handle type-specific value assignment.
 * @param is_equality_check_val Boolean, true for equality (is), false for inequality (is_not).
 * @param actual_val_param The canonical union for the actual value (passed by value).
 * @param expected_val_param The canonical union for the expected value (passed by value).
 * @param type_token_val The type token (e.g., double_tap_TYPE_INT, double_tap_TYPE_STRING).
 * @param description_fmt Format string for the description.
 * @param ... Arguments for the description format string.
 */
#define __double_tap_CORE_COMPARE(is_equality_check_val, actual_val_param, expected_val_param, type_token_val, description_fmt, ...)                      \
    do                                                                                                                                                    \
    {                                                                                                                                                     \
        __double_tap_init_context(); /* Ensure context is initialized before use */                                                                       \
        int current_depth = __double_tap_ctx.stack_idx;                                                                                                   \
        __double_tap_ctx.local_test_count++;                                                                                                              \
        bool _dt_comparison_result = __double_tap_perform_comparison(is_equality_check_val, type_token_val,                                               \
                                                                     &actual_val_param, &expected_val_param, double_tap_EPSILON);                         \
                                                                                                                                                          \
        _double_tap_print_test_line_base(_dt_comparison_result, current_depth, __double_tap_ctx.local_test_count, description_fmt, ##__VA_ARGS__);        \
        printf("\n");                                                                                                                                     \
                                                                                                                                                          \
        if (!_dt_comparison_result)                                                                                                                       \
        {                                                                                                                                                 \
            /* This is a real failure (not an expected TODO fail) */                                                                                      \
            __double_tap_ctx.subtest_failed_stack[current_depth] = true;                                                                                  \
                                                                                                                                                          \
            /* If it's a top-level test, add its number to the failed list */                                                                             \
            if (current_depth == 0)                                                                                                                       \
            {                                                                                                                                             \
                __double_tap_add_overall_failed_test_number(__double_tap_ctx.local_test_count);                                                           \
            }                                                                                                                                             \
                                                                                                                                                          \
            char val_buf_actual[128];                                                                                                                     \
            char val_buf_expected[128];                                                                                                                   \
                                                                                                                                                          \
            __double_tap_sprint_val_to_yaml_string(val_buf_actual, sizeof(val_buf_actual), type_token_val, &actual_val_param);                            \
            __double_tap_sprint_val_to_yaml_string(val_buf_expected, sizeof(val_buf_expected), type_token_val, &expected_val_param);                      \
                                                                                                                                                          \
            __double_tap_print_indent(stderr, current_depth);                                                                                             \
            fprintf(stderr, "  ---\n");                                                                                                                   \
            __double_tap_diag_printf_internal(current_depth + 1, "message: \"%s comparison failed\"", is_equality_check_val ? "Equality" : "Inequality"); \
            __double_tap_diag_printf_internal(current_depth + 1, "severity: fail");                                                                       \
            __double_tap_diag_printf_internal(current_depth + 1, "found: %s", val_buf_actual);                                                            \
            __double_tap_diag_printf_internal(current_depth + 1, "wanted: %s", val_buf_expected);                                                         \
            __double_tap_diag_printf_internal(current_depth + 1, "at:");                                                                                  \
            __double_tap_diag_printf_internal(current_depth + 2, "file: %s", __FILE__);                                                                   \
            __double_tap_diag_printf_internal(current_depth + 2, "line: %d", __LINE__);                                                                   \
            __double_tap_print_indent(stderr, current_depth);                                                                                             \
            fprintf(stderr, "  ...\n");                                                                                                                   \
        }                                                                                                                                                 \
    } while (0)

// --- User-Facing Macros ---

/**
 * @brief Reports a test result (pass/fail).
 * @param condition The boolean condition (true for pass, false for fail).
 * @param description_fmt Format string for the test description.
 * @param ... Arguments for the description format string.
 */
#define ok(condition, description_fmt, ...)                                                                                            \
    do                                                                                                                                 \
    {                                                                                                                                  \
        __double_tap_init_context(); /* Ensure context is initialized */                                                               \
        int current_depth = __double_tap_ctx.stack_idx;                                                                                \
        __double_tap_ctx.local_test_count++;                                                                                           \
        _double_tap_print_test_line_base(condition, current_depth, __double_tap_ctx.local_test_count, description_fmt, ##__VA_ARGS__); \
        printf("\n");                                                                                                                  \
        if (!condition)                                                                                                                \
        {                                                                                                                              \
            __double_tap_ctx.subtest_failed_stack[current_depth] = true;                                                               \
            if (current_depth == 0)                                                                                                    \
            {                                                                                                                          \
                __double_tap_add_overall_failed_test_number(__double_tap_ctx.local_test_count);                                        \
            }                                                                                                                          \
        }                                                                                                                              \
    } while (0)

/**
 * @brief Reports a test failure directly. Shortcut for ok(false, ...).
 * @param description_fmt Format string for the test description.
 * @param ... Arguments for the description format string.
 */
#define fail(description_fmt, ...) \
    ok(false, description_fmt, ##__VA_ARGS__)

/**
 * @brief Sets the expected number of tests. Prints "1..N" immediately if N != NO_PLAN.
 * @param n The expected number of tests. Use NO_PLAN if unknown, then done_testing() prints it.
 * For the top-level plan, it also prints "TAP version 14".
 */
#define plan(n)                                                          \
    do                                                                   \
    {                                                                    \
        __double_tap_init_context(); /* Ensure context is initialized */ \
        int current_depth = __double_tap_ctx.stack_idx;                  \
        if (current_depth == 0 && !__double_tap_ctx.tap_version_printed) \
        {                                                                \
            printf("TAP version 14\n");                                  \
            __double_tap_ctx.tap_version_printed = true;                 \
        }                                                                \
        __double_tap_print_indent(stdout, current_depth);                \
        if (n != NO_PLAN)                                                \
        {                                                                \
            printf("1..%d\n", n);                                        \
        }                                                                \
        if (current_depth > 0)                                           \
        {                                                                \
            __double_tap_ctx.explicit_plan_values[current_depth] = n;    \
        }                                                                \
        else                                                             \
        {                                                                \
            __double_tap_ctx.test_plan = n;                              \
        }                                                                \
    } while (0)

/**
 * @brief Reports a test as skipped.
 * @param description_fmt Format string for the test description.
 * @param ... Arguments for the description format string.
 */
#define skip(description_fmt, ...)                                                                                                \
    do                                                                                                                            \
    {                                                                                                                             \
        __double_tap_init_context(); /* Ensure context is initialized */                                                          \
        int current_depth = __double_tap_ctx.stack_idx;                                                                           \
        __double_tap_ctx.local_test_count++;                                                                                      \
        _double_tap_print_test_line_base(true, current_depth, __double_tap_ctx.local_test_count, description_fmt, ##__VA_ARGS__); \
        printf(" # SKIP\n");                                                                                                      \
    } while (0)

/**
 * @brief Reports all subsequent tests as skipped and either returns from the current subtest
 * or breaks from the enclosing `do {} while(0)` block at the top level.
 * @param description_fmt Format string for the skip message.
 * @param ... Arguments for the description format string.
 */
#define skip_all(description_fmt, ...)                                     \
    do                                                                     \
    {                                                                      \
        __double_tap_init_context(); /* Ensure context is initialized */   \
        int current_depth = __double_tap_ctx.stack_idx;                    \
        __double_tap_print_indent(stdout, current_depth);                  \
        printf("1..0 # Skip ");                                            \
        printf(description_fmt, ##__VA_ARGS__);                            \
        printf("\n");                                                      \
        __double_tap_ctx.skip_all_active = true; /* Set global flag */     \
        if (current_depth > 0)                                             \
        {           /* If inside a subtest */                              \
            return; /* Exit the current subtest function */                \
        }                                                                  \
        else                                                               \
        {          /* Top-level */                                         \
            break; /* Break out of the enclosing do {} while(0) in main */ \
        }                                                                  \
    } while (0) // The do/while(0) allows 'break' to function like exiting a block

/**
 * @brief Reports a test with a TODO directive.
 * Prints "ok N - description # TODO (unexpected pass)" if condition is true (marks failed),
 * or "not ok N - description # TODO" if condition is false.
 * A TODO test, regardless of outcome, DOES NOT cause its immediate parent subtest to report 'not ok'.
 * Furthermore, TODO tests DO NOT count towards overall harness failures or the list of failed tests.
 * Unexpected passes are tracked in a separate list for informational purposes.
 * @param condition The boolean condition (true for pass, false for fail).
 * @param description_fmt Format string for the test description.
 * @param ... Arguments for the description format string.
 */
#define todo(condition, description_fmt, ...)                                                                                          \
    do                                                                                                                                 \
    {                                                                                                                                  \
        __double_tap_init_context(); /* Ensure context is initialized */                                                               \
        int current_depth = __double_tap_ctx.stack_idx;                                                                                \
        __double_tap_ctx.local_test_count++;                                                                                           \
        if (condition)                                                                                                                 \
        {                                                                                                                              \
            /* Unexpected pass: 'ok' visually. Does NOT set subtest_failed_stack. */                                                   \
            _double_tap_print_test_line_base(true, current_depth, __double_tap_ctx.local_test_count, description_fmt, ##__VA_ARGS__);  \
            printf(" # TODO (unexpected pass)\n");                                                                                     \
            /* Determine the top-level test number for this TODO test */                                                               \
            int top_level_test_num_for_todo;                                                                                           \
            if (current_depth == 0)                                                                                                    \
            {                                                                                                                          \
                top_level_test_num_for_todo = __double_tap_ctx.local_test_count;                                                       \
            }                                                                                                                          \
            else                                                                                                                       \
            {                                                                                                                          \
                /* For nested subtests, use the local test count of the top-most parent. */                                            \
                top_level_test_num_for_todo = __double_tap_ctx.test_count_stack[0];                                                    \
            }                                                                                                                          \
            __double_tap_add_unexpected_todo_pass_number(top_level_test_num_for_todo);                                                 \
            /* Mark this subtest scope as having an unexpected TODO pass */                                                            \
            __double_tap_ctx.subtest_has_unexpected_todo_pass[current_depth] = true;                                                   \
        }                                                                                                                              \
        else                                                                                                                           \
        {                                                                                                                              \
            /* Expected fail: 'not ok' visually. Does NOT set subtest_failed_stack. */                                                 \
            _double_tap_print_test_line_base(false, current_depth, __double_tap_ctx.local_test_count, description_fmt, ##__VA_ARGS__); \
            printf(" # TODO\n");                                                                                                       \
            /* No impact on overall_failures or failed_test_numbers_list for expected TODO failures */                                 \
        }                                                                                                                              \
    } while (0)

/**
 * @brief Prints a "Bail out!" message and exits with status 255.
 * Used for unrecoverable errors.
 * @param message_fmt Format string for the bail out message.
 * @param ... Arguments for the message format string.
 */
#define BAIL_OUT(message_fmt, ...) \
    __double_tap_bail_out_internal(message_fmt, ##__VA_ARGS__)

/**
 * @brief Prints a diagnostic message. Shortcut for __double_tap_diag_printf_internal.
 * @param message_fmt Format string for the message.
 * @param ... Arguments for the message format string.
 */
#define diag(message_fmt, ...) \
    __double_tap_diag_printf_internal(__double_tap_ctx.stack_idx, message_fmt, ##__VA_ARGS__)

/**
 * @brief Prints a general note. Shortcut for __double_tap_note_printf_internal.
 * @param message_fmt Format string for the message.
 * @param ... Arguments for the message format string.
 */
#define note(message_fmt, ...) \
    __double_tap_note_printf_internal(__double_tap_ctx.stack_idx, message_fmt, ##__VA_ARGS__)

/**
 * @brief Runs a subtest.
 * The subtest body must be provided as a function pointer to a void function with no arguments.
 * @param description_fmt Format string for the subtest description.
 * @param func_ptr A function pointer to the subtest body (e.g., &my_subtest_function).
 * @param ... Arguments for the description format string.
 */
#define subtest(description_fmt, func_ptr, ...)                                                                                                        \
    do                                                                                                                                                 \
    {                                                                                                                                                  \
        __double_tap_init_context(); /* Ensure context is initialized */                                                                               \
        /* Increment parent's local_test_count to assign a test number to THIS subtest itself */                                                       \
        __double_tap_ctx.local_test_count++;                                                                                                           \
        int __double_tap_subtest_this_test_num = __double_tap_ctx.local_test_count;                                                                    \
                                                                                                                                                       \
        /* Save current parent's explicit plan info before pushing to new stack level */                                                               \
        int __double_tap_subtest_parent_explicit_plan_val_saved = __double_tap_ctx.explicit_plan_values[__double_tap_ctx.stack_idx];                   \
        /* Save parent's current HAS_TODO status */                                                                                                    \
        bool __double_tap_subtest_parent_has_todo_saved = __double_tap_ctx.subtest_has_unexpected_todo_pass[__double_tap_ctx.stack_idx];               \
                                                                                                                                                       \
        /* Store the current test number (which is this subtest's test number) at the current stack depth */                                           \
        /* This value will be retrieved by children (e.g., TODOs) at test_count_stack[0] */                                                            \
        __double_tap_ctx.test_count_stack[__double_tap_ctx.stack_idx] = __double_tap_subtest_this_test_num;                                            \
                                                                                                                                                       \
        /* Store the current depth for indentation of subtest's own tests/plan */                                                                      \
        int __double_tap_subtest_level_indent = __double_tap_ctx.stack_idx;                                                                            \
                                                                                                                                                       \
        /* Check nesting depth before incrementing stack_idx */                                                                                        \
        if (__double_tap_ctx.stack_idx + 1 >= double_tap_MAX_NESTING)                                                                                  \
        {                                                                                                                                              \
            __double_tap_bail_out_internal("Subtest nesting too deep! Max nesting: %d", double_tap_MAX_NESTING);                                       \
        }                                                                                                                                              \
                                                                                                                                                       \
        /* Increment stack index to represent entry into new subtest scope */                                                                          \
        __double_tap_ctx.stack_idx++;                                                                                                                  \
                                                                                                                                                       \
        /* Reset local count and failure flag for the new subtest's internal tests */                                                                  \
        __double_tap_ctx.local_test_count = 0;                                                                                                         \
        __double_tap_ctx.subtest_failed_stack[__double_tap_ctx.stack_idx] = false;                                                                     \
        __double_tap_ctx.explicit_plan_values[__double_tap_ctx.stack_idx] = NO_PLAN;                                                                   \
        /* Reset child's HAS_TODO status */                                                                                                            \
        __double_tap_ctx.subtest_has_unexpected_todo_pass[__double_tap_ctx.stack_idx] = false;                                                         \
                                                                                                                                                       \
        /* Dynamically allocate and store the subtest description */                                                                                   \
        char *__double_tap_current_subtest_desc = NULL;                                                                                                \
        int __double_tap_len_needed = snprintf(NULL, 0, description_fmt, ##__VA_ARGS__);                                                               \
        if (__double_tap_len_needed >= 0)                                                                                                              \
        {                                                                                                                                              \
            __double_tap_current_subtest_desc = (char *)malloc((size_t)__double_tap_len_needed + 1);                                                   \
            if (__double_tap_current_subtest_desc)                                                                                                     \
            {                                                                                                                                          \
                snprintf(__double_tap_current_subtest_desc, (size_t)__double_tap_len_needed + 1, description_fmt, ##__VA_ARGS__);                      \
            }                                                                                                                                          \
            else                                                                                                                                       \
            {                                                                                                                                          \
                __double_tap_bail_out_internal("Failed to allocate memory for subtest description.");                                                  \
            }                                                                                                                                          \
        }                                                                                                                                              \
        __double_tap_ctx.subtest_desc_stack[__double_tap_ctx.stack_idx] = __double_tap_current_subtest_desc;                                           \
                                                                                                                                                       \
        /* Print subtest header (this is a diagnostic line, so it starts with #) */                                                                    \
        __double_tap_print_indent(stdout, __double_tap_ctx.stack_idx);                                                                                 \
        printf("# Subtest: %s\n", __double_tap_current_subtest_desc ? __double_tap_current_subtest_desc : "Untitled Subtest");                         \
                                                                                                                                                       \
        /* Execute the subtest function */                                                                                                             \
        if (func_ptr)                                                                                                                                  \
        {                                                                                                                                              \
            ((void (*)())func_ptr)();                                                                                                                  \
        }                                                                                                                                              \
        else                                                                                                                                           \
        {                                                                                                                                              \
            fail("Subtest function pointer is NULL for \"%s\"", __double_tap_current_subtest_desc);                                                    \
        }                                                                                                                                              \
                                                                                                                                                       \
        /* --- Subtest Cleanup and Result Reporting --- */                                                                                             \
                                                                                                                                                       \
        bool __double_tap_subtest_did_any_non_todo_fail = __double_tap_ctx.subtest_failed_stack[__double_tap_ctx.stack_idx];                           \
        /* Capture child's HAS_TODO status before stack pop */                                                                                         \
        bool __double_tap_subtest_had_unexpected_todo_pass_local_copy = __double_tap_ctx.subtest_has_unexpected_todo_pass[__double_tap_ctx.stack_idx]; \
                                                                                                                                                       \
        int __double_tap_subtest_final_count = __double_tap_ctx.local_test_count;                                                                      \
        int __double_tap_subtest_actual_plan_value = __double_tap_ctx.explicit_plan_values[__double_tap_ctx.stack_idx];                                \
                                                                                                                                                       \
        __double_tap_ctx.stack_idx--;                                                                                                                  \
        __double_tap_ctx.local_test_count = __double_tap_subtest_this_test_num;                                                                        \
        __double_tap_ctx.explicit_plan_values[__double_tap_ctx.stack_idx] = __double_tap_subtest_parent_explicit_plan_val_saved;                       \
                                                                                                                                                       \
        /* Propagate NON-TODO failure up */                                                                                                            \
        if (__double_tap_subtest_did_any_non_todo_fail)                                                                                                \
        {                                                                                                                                              \
            __double_tap_ctx.subtest_failed_stack[__double_tap_ctx.stack_idx] = true;                                                                  \
        }                                                                                                                                              \
        /* Propagate unexpected TODO pass status up */                                                                                                 \
        if (__double_tap_subtest_had_unexpected_todo_pass_local_copy)                                                                                  \
        {                                                                                                                                              \
            __double_tap_ctx.subtest_has_unexpected_todo_pass[__double_tap_ctx.stack_idx] = true;                                                      \
        }                                                                                                                                              \
                                                                                                                                                       \
        /* Print subtest's plan line */                                                                                                                \
        if (__double_tap_subtest_actual_plan_value == NO_PLAN ||                                                                                       \
            __double_tap_subtest_actual_plan_value != __double_tap_subtest_final_count)                                                                \
        {                                                                                                                                              \
            __double_tap_print_indent(stdout, __double_tap_subtest_level_indent + 1);                                                                  \
            printf("1..%d\n", __double_tap_subtest_final_count);                                                                                       \
        }                                                                                                                                              \
                                                                                                                                                       \
        /* Emit the subtest's overall result in the parent's context */                                                                                \
        /* IMPORTANT: The "Subtest %s: %s" format string below is directly passed to _double_tap_print_test_line_base. */                              \
        /* The _double_tap_print_test_line_base function internally calls __double_tap_vprint_escaped_string, */                                       \
        /* which escapes # and \ characters *within* the formatted description. */                                                                     \
        /* This means the 'Subtest: ' part will be printed as literal text, and if the subtest description_fmt */                                      \
        /* itself contains # or \, those will be escaped. */                                                                                           \
        _double_tap_print_test_line_base(!__double_tap_subtest_did_any_non_todo_fail,                                                                  \
                                         __double_tap_subtest_level_indent,                                                                            \
                                         __double_tap_subtest_this_test_num,                                                                           \
                                         "Subtest: %s: %s", /* No leading '#' here as it's part of the test name, not a comment */                     \
                                         __double_tap_current_subtest_desc ? __double_tap_current_subtest_desc : "Untitled Subtest",                   \
                                         __double_tap_subtest_did_any_non_todo_fail ? "Failed" : "Passed");                                            \
                                                                                                                                                       \
        /* Conditionally append # TODO if the subtest passed but had an unexpected TODO pass */                                                        \
        if (!__double_tap_subtest_did_any_non_todo_fail && __double_tap_subtest_had_unexpected_todo_pass_local_copy)                                   \
        {                                                                                                                                              \
            printf(" # TODO");                                                                                                                         \
        }                                                                                                                                              \
        printf("\n");                                                                                                                                  \
                                                                                                                                                       \
        /* If the subtest truly failed (non-TODO failure), and it's a top-level subtest, add its number to the failed list */                          \
        if (__double_tap_subtest_did_any_non_todo_fail && __double_tap_subtest_level_indent == 0)                                                      \
        {                                                                                                                                              \
            __double_tap_add_overall_failed_test_number(__double_tap_subtest_this_test_num);                                                           \
        }                                                                                                                                              \
                                                                                                                                                       \
        /* Free dynamically allocated subtest description */                                                                                           \
        if (__double_tap_current_subtest_desc)                                                                                                         \
        {                                                                                                                                              \
            free(__double_tap_current_subtest_desc);                                                                                                   \
            __double_tap_ctx.subtest_desc_stack[__double_tap_ctx.stack_idx + 1] = NULL;                                                                \
        }                                                                                                                                              \
    } while (0)

// --- New Type-Specific User-Facing Comparison Macros ---

/**
 * @brief Asserts that two chars are equal.
 */
#define is_char(actual, expected, description_fmt, ...)                                  \
    do                                                                                   \
    {                                                                                    \
        __double_tap_canonical_val actual_val_local;                                     \
        actual_val_local.ll_val = (long long)(actual);                                   \
        __double_tap_canonical_val expected_val_local;                                   \
        expected_val_local.ll_val = (long long)(expected);                               \
        __double_tap_CORE_COMPARE(true, actual_val_local, expected_val_local,            \
                                  double_tap_TYPE_CHAR, description_fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief Asserts that two chars are not equal.
 */
#define is_not_char(actual, expected, description_fmt, ...)                              \
    do                                                                                   \
    {                                                                                    \
        __double_tap_canonical_val actual_val_local;                                     \
        actual_val_local.ll_val = (long long)(actual);                                   \
        __double_tap_canonical_val expected_val_local;                                   \
        expected_val_local.ll_val = (long long)(expected);                               \
        __double_tap_CORE_COMPARE(false, actual_val_local, expected_val_local,           \
                                  double_tap_TYPE_CHAR, description_fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief Asserts that two integers are equal.
 */
#define is_int(actual, expected, description_fmt, ...)                                  \
    do                                                                                  \
    {                                                                                   \
        __double_tap_canonical_val actual_val_local;                                    \
        actual_val_local.ll_val = (long long)(actual);                                  \
        __double_tap_canonical_val expected_val_local;                                  \
        expected_val_local.ll_val = (long long)(expected);                              \
        __double_tap_CORE_COMPARE(true, actual_val_local, expected_val_local,           \
                                  double_tap_TYPE_INT, description_fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief Asserts that two integers are not equal.
 */
#define is_not_int(actual, expected, description_fmt, ...)                              \
    do                                                                                  \
    {                                                                                   \
        __double_tap_canonical_val actual_val_local;                                    \
        actual_val_local.ll_val = (long long)(actual);                                  \
        __double_tap_canonical_val expected_val_local;                                  \
        expected_val_local.ll_val = (long long)(expected);                              \
        __double_tap_CORE_COMPARE(false, actual_val_local, expected_val_local,          \
                                  double_tap_TYPE_INT, description_fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief Asserts that two floats are approximately equal.
 */
#define is_float(actual, expected, description_fmt, ...)                                  \
    do                                                                                    \
    {                                                                                     \
        __double_tap_canonical_val actual_val_local;                                      \
        actual_val_local.d_val = (double)(actual);                                        \
        __double_tap_canonical_val expected_val_local;                                    \
        expected_val_local.d_val = (double)(expected);                                    \
        __double_tap_CORE_COMPARE(true, actual_val_local, expected_val_local,             \
                                  double_tap_TYPE_FLOAT, description_fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief Asserts that two floats are not approximately equal.
 */
#define is_not_float(actual, expected, description_fmt, ...)                              \
    do                                                                                    \
    {                                                                                     \
        __double_tap_canonical_val actual_val_local;                                      \
        actual_val_local.d_val = (double)(actual);                                        \
        __double_tap_canonical_val expected_val_local;                                    \
        expected_val_local.d_val = (double)(expected);                                    \
        __double_tap_CORE_COMPARE(false, actual_val_local, expected_val_local,            \
                                  double_tap_TYPE_FLOAT, description_fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief Asserts that two doubles are approximately equal.
 */
#define is_double(actual, expected, description_fmt, ...)                                  \
    do                                                                                     \
    {                                                                                      \
        __double_tap_canonical_val actual_val_local;                                       \
        actual_val_local.d_val = (double)(actual);                                         \
        __double_tap_canonical_val expected_val_local;                                     \
        expected_val_local.d_val = (double)(expected);                                     \
        __double_tap_CORE_COMPARE(true, actual_val_local, expected_val_local,              \
                                  double_tap_TYPE_DOUBLE, description_fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief Asserts that two doubles are not approximately equal.
 */
#define is_not_double(actual, expected, description_fmt, ...)                              \
    do                                                                                     \
    {                                                                                      \
        __double_tap_canonical_val actual_val_local;                                       \
        actual_val_local.d_val = (double)(actual);                                         \
        __double_tap_canonical_val expected_val_local;                                     \
        expected_val_local.d_val = (double)(expected);                                     \
        __double_tap_CORE_COMPARE(false, actual_val_local, expected_val_local,             \
                                  double_tap_TYPE_DOUBLE, description_fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief Asserts that two strings are equal.
 * @param actual_string The actual string.
 * @param expected_string The expected string.
 * @param description_fmt Format string for the test description.
 * @param ... Arguments for the description format string.
 */
#define is_str(actual_string, expected_string, description_fmt, ...)                       \
    do                                                                                     \
    {                                                                                      \
        __double_tap_canonical_val actual_val_local;                                       \
        actual_val_local.s_ptr_val = (const char *)(actual_string);                        \
        __double_tap_canonical_val expected_val_local;                                     \
        expected_val_local.s_ptr_val = (const char *)(expected_string);                    \
        __double_tap_CORE_COMPARE(true, actual_val_local, expected_val_local,              \
                                  double_tap_TYPE_STRING, description_fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief Asserts that two strings are not equal.
 * @param actual_string The actual string.
 * @param expected_string The expected string.
 * @param description_fmt Format string for the test description.
 * @param ... Arguments for the description format string.
 */
#define is_not_str(actual_string, expected_string, description_fmt, ...)                   \
    do                                                                                     \
    {                                                                                      \
        __double_tap_canonical_val actual_val_local;                                       \
        actual_val_local.s_ptr_val = (const char *)(actual_string);                        \
        __double_tap_canonical_val expected_val_local;                                     \
        expected_val_local.s_ptr_val = (const char *)(expected_string);                    \
        __double_tap_CORE_COMPARE(false, actual_val_local, expected_val_local,             \
                                  double_tap_TYPE_STRING, description_fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief Asserts that two pointers are equal.
 */
#define is_ptr(actual, expected, description_fmt, ...)                                  \
    do                                                                                  \
    {                                                                                   \
        __double_tap_canonical_val actual_val_local;                                    \
        actual_val_local.p_val = (const void *)(actual);                                \
        __double_tap_canonical_val expected_val_local;                                  \
        expected_val_local.p_val = (const void *)(expected);                            \
        __double_tap_CORE_COMPARE(true, actual_val_local, expected_val_local,           \
                                  double_tap_TYPE_PTR, description_fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief Asserts that two pointers are not equal.
 */
#define is_not_ptr(actual, expected, description_fmt, ...)                              \
    do                                                                                  \
    {                                                                                   \
        __double_tap_canonical_val actual_val_local;                                    \
        actual_val_local.p_val = (const void *)(actual);                                \
        __double_tap_canonical_val expected_val_local;                                  \
        expected_val_local.p_val = (const void *)(expected);                            \
        __double_tap_CORE_COMPARE(false, actual_val_local, expected_val_local,          \
                                  double_tap_TYPE_PTR, description_fmt, ##__VA_ARGS__); \
    } while (0)

/**
 * @brief Formats a sorted list of integer test numbers into a string with ranges (e.g., "1, 3-5, 8").
 * @param numbers The sorted array of integer test numbers.
 * @param count The number of elements in the array.
 * @param buffer The character buffer to write the formatted string into.
 * @param buffer_size The size of the buffer.
 * @return The number of characters written to the buffer, excluding the null terminator.
 */
static inline int __double_tap_format_test_number_list(int *numbers, size_t count, char *buffer, size_t buffer_size)
{
    if (count == 0)
    {
        if (buffer_size > 0)
            buffer[0] = '\0';
        return 0;
    }

    size_t offset = 0; // Changed to size_t
    int current_start = numbers[0];
    int current_end = numbers[0];

    for (size_t i = 1; i < count; ++i)
    {
        if (numbers[i] == current_end + 1)
        {
            current_end = numbers[i];
        }
        else
        {
            if (current_start == current_end)
            { // Single number
                offset += (size_t)snprintf(buffer + offset, buffer_size - offset, "%d, ", current_start);
            }
            else
            { // Range
                offset += (size_t)snprintf(buffer + offset, buffer_size - offset, "%d-%d, ", current_start, current_end);
            }
            current_start = numbers[i];
            current_end = numbers[i];
        }
        // Basic realloc check (can be more robust with iterative realloc)
        if (offset >= buffer_size - 20)
        {          // Keep margin for next item
            break; // Avoid buffer overflow in simple inline, might truncate list
        }
    }

    // Print the last pending range/number
    if (current_start != -1)
    {
        if (current_start == current_end)
        {
            offset += (size_t)snprintf(buffer + offset, buffer_size - offset, "%d", current_start);
        }
        else
        {
            offset += (size_t)snprintf(buffer + offset, buffer_size - offset, "%d-%d", current_start, current_end);
        }
    }

    // Remove trailing ", " if it exists from intermediate snprintf calls
    if (offset > 2 && buffer[offset - 2] == ',' && buffer[offset - 1] == ' ')
    {
        buffer[offset - 2] = '\0';
        offset -= 2;
    }
    else
    {
        buffer[offset] = '\0'; // Ensure null termination
    }

    return (int)offset; // Cast back to int as return type is int
}

/**
 * @brief Finishes testing and prints the final plan and summary.
 * If plan(NO_PLAN) was used, this will print the final plan line.
 * Returns the number of failed tests.
 */
static inline int done_testing(void)
{
    __double_tap_init_context();                                        // Ensure context is initialized at done_testing as well
    int total_tests = __double_tap_ctx.local_test_count;                // Total tests at the top level
    int passed_tests = total_tests - __double_tap_ctx.overall_failures; // Calculate passed tests

    if (__double_tap_ctx.test_plan == NO_PLAN)
    {
        __double_tap_print_indent(stdout, 0); // Ensure top-level plan is also indented (0 spaces)
        printf("1..%d\n", total_tests);
    }
    else if (__double_tap_ctx.test_plan != total_tests)
    {
        __double_tap_print_indent(stdout, 0);          // Ensure top-level plan is also indented (0 spaces)
        printf("1..%d\n", __double_tap_ctx.test_plan); // Print the planned count
        __double_tap_diag_printf_internal(0, "Plan mismatch! Expected %d tests, ran %d.", __double_tap_ctx.test_plan, total_tests);
        // This is a top-level failure, so add it to the overall failures count if it's the first one
        if (__double_tap_ctx.overall_failures == 0)
        {                                        // Only increment if no other failures exist from plan mismatch
            __double_tap_ctx.overall_failures++; // Increment failure count for plan mismatch
        }
    }

    // Print summary to stderr (as diagnostics)
    fprintf(stderr, "# --- Test Summary ---\n");
    fprintf(stderr, "# Total Tests (top-level): %d\n", total_tests);
    fprintf(stderr, "# Passed (top-level):      %d\n", passed_tests);
    fprintf(stderr, "# Failed (top-level):      %d\n", __double_tap_ctx.overall_failures);
    fprintf(stderr, "# --------------------\n");

    if (__double_tap_ctx.overall_failures > 0)
    {
        if (__double_tap_ctx.failed_tests_count > 0)
        {
            qsort(__double_tap_ctx.failed_test_numbers_list, __double_tap_ctx.failed_tests_count,
                  sizeof(int), __double_tap_compare_ints);

            char formatted_list_buffer[512]; // Sufficient buffer for typical lists
            __double_tap_format_test_number_list(__double_tap_ctx.failed_test_numbers_list,
                                                 __double_tap_ctx.failed_tests_count,
                                                 formatted_list_buffer, sizeof(formatted_list_buffer));
            __double_tap_diag_printf_internal(0, "Failing Tests (by number): %s", formatted_list_buffer);
        }
        else if (__double_tap_ctx.overall_failures > 0 && __double_tap_ctx.test_plan != total_tests)
        {
            // If overall_failures is > 0 but failed_tests_count is 0, it implies only a plan mismatch
            __double_tap_diag_printf_internal(0, "Note: The only top-level failure was a plan mismatch.");
        }
    }

    // List Unexpectedly Passing TODO Tests
    if (__double_tap_ctx.unexpected_todo_passes_count > 0)
    {
        qsort(__double_tap_ctx.unexpected_todo_passes_list, __double_tap_ctx.unexpected_todo_passes_count,
              sizeof(int), __double_tap_compare_ints);

        char formatted_todo_list_buffer[512];
        __double_tap_format_test_number_list(__double_tap_ctx.unexpected_todo_passes_list,
                                             __double_tap_ctx.unexpected_todo_passes_count,
                                             formatted_todo_list_buffer, sizeof(formatted_todo_list_buffer));
        __double_tap_diag_printf_internal(0, "Unexpectedly Passing TODO Tests (by number): %s", formatted_todo_list_buffer);
    }

    // Free dynamically allocated memory for failed test numbers
    free(__double_tap_ctx.failed_test_numbers_list);
    __double_tap_ctx.failed_test_numbers_list = NULL;
    __double_tap_ctx.failed_tests_count = 0;
    __double_tap_ctx.failed_tests_capacity = 0;

    // Free dynamically allocated memory for unexpected TODO passes
    free(__double_tap_ctx.unexpected_todo_passes_list);
    __double_tap_ctx.unexpected_todo_passes_list = NULL;
    __double_tap_ctx.unexpected_todo_passes_count = 0;
    __double_tap_ctx.unexpected_todo_passes_capacity = 0;

    return __double_tap_ctx.overall_failures;
}

#define is_size_t(actual, expected, description_fmt, ...) ((void)0)
#define is_wchar_t(actual, expected, description_fmt, ...) ((void)0)
#define is_int128(actual, expected, description_fmt, ...)                                                                                                                   \
    do                                                                                                                                                                      \
    { /*  (unsigned long long)(g_ret_storage.i128_val >> 64), (unsigned long long)g_ret_storage.i128_val, (unsigned long long)(in_val >> 64), (unsigned long long)in_val)*/ \
        __double_tap_canonical_val actual_val_local;                                                                                                                        \
        actual_val_local.i128 = (long long)(actual);                                                                                                                        \
        __double_tap_canonical_val expected_val_local;                                                                                                                      \
        expected_val_local.i128 = (long long)(expected);                                                                                                                    \
        __double_tap_CORE_COMPARE(true, actual_val_local, expected_val_local,                                                                                               \
                                  double_tap_TYPE_INT128, description_fmt, ##__VA_ARGS__);                                                                                  \
    } while (0)

#define is_uint128(actual, expected, description_fmt, ...) ((void)0)
#else // FFI_TESTING is 0 or undefined (framework disabled)

// --- No-op definitions when FFI_TESTING is disabled ---
#define double_tap_MAX_NESTING 0
#define double_tap_EPSILON 0.0
#define NO_PLAN 0

// No-op macros
#define plan(n) ((void)0)
#define ok(condition, description_fmt, ...) ((void)0)
#define fail(description_fmt, ...) ((void)0)
#define skip(description_fmt, ...) ((void)0)
#define skip_all(description_fmt, ...) ((void)0)
#define todo(condition, description_fmt, ...) ((void)0)

// When FFI_TESTING is disabled, BAIL_OUT also becomes a no-op
#define BAIL_OUT(message_fmt, ...) ((void)0)

#define diag(message_fmt, ...) ((void)0)
#define note(message_fmt, ...) ((void)0)
#define subtest(description_fmt, func_ptr, ...) ((void)0)
#define is_char(actual, expected, description_fmt, ...) ((void)0)
#define is_not_char(actual, expected, description_fmt, ...) ((void)0)
#define is_int(actual, expected, description_fmt, ...) ((void)0)
#define is_not_int(actual, expected, description_fmt, ...) ((void)0)
#define is_float(actual, expected, description_fmt, ...) ((void)0)
#define is_not_float(actual, expected, description_fmt, ...) ((void)0)
#define is_double(actual, expected, description_fmt, ...) ((void)0)
#define is_not_double(actual, expected, description_fmt, ...) ((void)0)
#define is_str(actual_string, expected_string, description_fmt, ...) ((void)0)
#define is_not_str(actual_string, expected_string, description_fmt, ...) ((void)0)
#define is_ptr(actual, expected, description_fmt, ...) ((void)0)
#define is_not_ptr(actual, expected, description_fmt, ...) ((void)0)
static inline int done_testing(void) { return 0; }
static inline void double_tap_set_color_output(bool enable) { (void)enable; }

#endif // FFI_TESTING

#endif // DOUBLE_TAP_H


# the type of the data to be represented
sourcetype = 'unsigned char'

# the type of the sparse array to be created
targettype = 'sparse_arr'

# the numbers of bits at each level of indirection
# the numbers should be in order from largest to smallest chunk size
# that is, the last number is the size (in bits) of the chunks that hold
# the source types directly.
bits = [5,2,3]

# the default (common) value
default = '0x00'

# the function to use for allocating memory
malloc = 'malloc'

# the function to use to free memory
free = 'free'

# whether to declare all functions as static
static = ''

# whether to inline lookup
inline_lookup = 'inline'

# whether to inline insert
inline_insert = 'inline'


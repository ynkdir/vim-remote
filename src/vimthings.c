
#include "vimthings.h"

/************************************************************************
 * Functions for handling growing arrays.
 */

/*
 * Clear an allocated growing array.
 */
    void
ga_clear(gap)
    garray_T *gap;
{
    vim_free(gap->ga_data);
    ga_init(gap);
}

/*
 * Clear a growing array that contains a list of strings.
 */
    void
ga_clear_strings(gap)
    garray_T *gap;
{
    int		i;

    for (i = 0; i < gap->ga_len; ++i)
	vim_free(((char_u **)(gap->ga_data))[i]);
    ga_clear(gap);
}

/*
 * Initialize a growing array.	Don't forget to set ga_itemsize and
 * ga_growsize!  Or use ga_init2().
 */
    void
ga_init(gap)
    garray_T *gap;
{
    gap->ga_data = NULL;
    gap->ga_maxlen = 0;
    gap->ga_len = 0;
}

    void
ga_init2(gap, itemsize, growsize)
    garray_T	*gap;
    int		itemsize;
    int		growsize;
{
    ga_init(gap);
    gap->ga_itemsize = itemsize;
    gap->ga_growsize = growsize;
}

/*
 * Make room in growing array "gap" for at least "n" items.
 * Return FAIL for failure, OK otherwise.
 */
    int
ga_grow(gap, n)
    garray_T	*gap;
    int		n;
{
    size_t	len;
    char_u	*pp;

    if (gap->ga_maxlen - gap->ga_len < n)
    {
	if (n < gap->ga_growsize)
	    n = gap->ga_growsize;
	len = gap->ga_itemsize * (gap->ga_len + n);
	pp = alloc_clear((unsigned)len);
	if (pp == NULL)
	    return FAIL;
	gap->ga_maxlen = gap->ga_len + n;
	if (gap->ga_data != NULL)
	{
	    mch_memmove(pp, gap->ga_data,
				      (size_t)(gap->ga_itemsize * gap->ga_len));
	    vim_free(gap->ga_data);
	}
	gap->ga_data = pp;
    }
    return OK;
}

/*
 * For a growing array that contains a list of strings: concatenate all the
 * strings with a separating comma.
 * Returns NULL when out of memory.
 */
    char_u *
ga_concat_strings(gap)
    garray_T *gap;
{
    int		i;
    int		len = 0;
    char_u	*s;

    for (i = 0; i < gap->ga_len; ++i)
	len += (int)STRLEN(((char_u **)(gap->ga_data))[i]) + 1;

    s = alloc(len + 1);
    if (s != NULL)
    {
	*s = NUL;
	for (i = 0; i < gap->ga_len; ++i)
	{
	    if (*s != NUL)
		STRCAT(s, ",");
	    STRCAT(s, ((char_u **)(gap->ga_data))[i]);
	}
    }
    return s;
}

/*
 * Concatenate a string to a growarray which contains characters.
 * Note: Does NOT copy the NUL at the end!
 */
    void
ga_concat(gap, s)
    garray_T	*gap;
    char_u	*s;
{
    int    len = (int)STRLEN(s);

    if (ga_grow(gap, len) == OK)
    {
	mch_memmove((char *)gap->ga_data + gap->ga_len, s, (size_t)len);
	gap->ga_len += len;
    }
}

/*
 * Append one byte to a growarray which contains bytes.
 */
    void
ga_append(gap, c)
    garray_T	*gap;
    int		c;
{
    if (ga_grow(gap, 1) == OK)
    {
	*((char *)gap->ga_data + gap->ga_len) = c;
	++gap->ga_len;
    }
}

/*
 * skipwhite: skip over ' ' and '\t'.
 */
    char_u *
skipwhite(q)
    char_u	*q;
{
    char_u	*p = q;

    while (vim_iswhite(*p)) /* skip to next non-white */
	++p;
    return p;
}

/*
 * Variant of isdigit() that can handle characters > 0x100.
 * We don't use isdigit() here, because on some systems it also considers
 * superscript 1 to be a digit.
 * Use the VIM_ISDIGIT() macro for simple arguments.
 */
    int
vim_isdigit(c)
    int		c;
{
    return (c >= '0' && c <= '9');
}

/*
 * Variant of isxdigit() that can handle characters > 0x100.
 * We don't use isxdigit() here, because on some systems it also considers
 * superscript 1 to be a digit.
 */
    int
vim_isxdigit(c)
    int		c;
{
    return (c >= '0' && c <= '9')
	|| (c >= 'a' && c <= 'f')
	|| (c >= 'A' && c <= 'F');
}


/*
 * Return the value of a single hex character.
 * Only valid when the argument is '0' - '9', 'A' - 'F' or 'a' - 'f'.
 */
    int
hex2nr(c)
    int		c;
{
    if (c >= 'a' && c <= 'f')
	return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
	return c - 'A' + 10;
    return c - '0';
}


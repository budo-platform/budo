#include "core/ts_strip.h"
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static bool is_ident_start(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
           c == '_' || c == '$';
}

static bool is_ident_char(char c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

static void blank(char *buf, size_t from, size_t to)
{
    for (size_t i = from; i < to; i++)
    {
        if (buf[i] != '\n' && buf[i] != '\r')
            buf[i] = ' ';
    }
}

static size_t read_ident(const char *buf, size_t pos, size_t len)
{
    if (pos >= len || !is_ident_start(buf[pos]))
        return 0;
    size_t start = pos;
    while (pos < len && is_ident_char(buf[pos]))
        pos++;
    return pos - start;
}

static bool ident_eq(const char *buf, size_t pos, size_t n, const char *word)
{
    size_t wlen = strlen(word);
    return n == wlen && memcmp(buf + pos, word, wlen) == 0;
}

static size_t skip_ws(const char *buf, size_t pos, size_t len)
{
    while (pos < len)
    {
        char c = buf[pos];
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
        {
            pos++;
        }
        else if (pos + 1 < len && c == '/' && buf[pos + 1] == '/')
        {
            
            pos += 2;
            while (pos < len && buf[pos] != '\n')
                pos++;
        }
        else if (pos + 1 < len && c == '/' && buf[pos + 1] == '*')
        {
            
            pos += 2;
            while (pos + 1 < len && !(buf[pos] == '*' && buf[pos + 1] == '/'))
                pos++;
            if (pos + 1 < len)
                pos += 2;
        }
        else
        {
            break;
        }
    }
    return pos;
}

static size_t skip_balanced(const char *buf, size_t pos, size_t len,
                            char opener, char closer)
{
    if (pos >= len || buf[pos] != opener)
        return pos;
    int depth = 1;
    pos++;
    while (pos < len && depth > 0)
    {
        char c = buf[pos];
        if (c == opener)
            depth++;
        else if (c == closer)
            depth--;
        else if (c == '\'' || c == '"' || c == '`')
        {
            
            char q = c;
            pos++;
            while (pos < len)
            {
                if (buf[pos] == '\\')
                {
                    pos += 2;
                    continue;
                }
                if (buf[pos] == q)
                {
                    if (q != '`')
                        break;
                    break; 
                }
                pos++;
            }
        }
        else if (pos + 1 < len && c == '/' && buf[pos + 1] == '/')
        {
            while (pos < len && buf[pos] != '\n')
                pos++;
            continue;
        }
        else if (pos + 1 < len && c == '/' && buf[pos + 1] == '*')
        {
            pos += 2;
            while (pos + 1 < len && !(buf[pos] == '*' && buf[pos + 1] == '/'))
                pos++;
            if (pos + 1 < len)
                pos += 2;
            continue;
        }
        pos++;
    }
    return pos;
}

static size_t skip_type_expr(const char *buf, size_t pos, size_t len)
{
    int needs_more = 1; 

    while (pos < len)
    {
        pos = skip_ws(buf, pos, len);
        if (pos >= len)
            break;

        char c = buf[pos];

        if (c == ',' || c == ')' || c == ']' || c == '}' || c == ';' ||
            c == '=' || c == '{')
            break;

        if (c == '=' && pos + 1 < len && buf[pos + 1] == '>')
            break;

        if (c == '(')
        {
            
            pos = skip_balanced(buf, pos, len, '(', ')');
            
            size_t after = skip_ws(buf, pos, len);
            if (after + 1 < len && buf[after] == '=' && buf[after + 1] == '>')
            {
                pos = after + 2;
                needs_more = 1;
                continue;
            }
            needs_more = 0;
            continue;
        }

        if (c == '[')
        {
            pos = skip_balanced(buf, pos, len, '[', ']');
            needs_more = 0;
            continue;
        }

        if (c == '<')
        {
            pos = skip_balanced(buf, pos, len, '<', '>');
            needs_more = 0;
            continue;
        }

        if (c == '{')
        {
            
            pos = skip_balanced(buf, pos, len, '{', '}');
            needs_more = 0;
            continue;
        }

        if (c == '|' || c == '&')
        {
            pos++;
            needs_more = 1;
            continue;
        }

        if (c == '.' && pos + 2 < len && buf[pos + 1] == '.' && buf[pos + 2] == '.')
        {
            
            break;
        }

        if (c == '.')
        {
            
            pos++;
            needs_more = 1;
            continue;
        }

        if (c == '\'' || c == '"')
        {
            char q = c;
            pos++;
            while (pos < len)
            {
                if (buf[pos] == '\\')
                {
                    pos += 2;
                    continue;
                }
                if (buf[pos] == q)
                    break;
                pos++;
            }
            if (pos < len)
                pos++;
            needs_more = 0;
            continue;
        }

        if (is_ident_start(c))
        {
            size_t id_len = read_ident(buf, pos, len);
            if (id_len == 0)
                break;

            if (ident_eq(buf, pos, id_len, "typeof") ||
                ident_eq(buf, pos, id_len, "keyof") ||
                ident_eq(buf, pos, id_len, "infer") ||
                ident_eq(buf, pos, id_len, "readonly") ||
                ident_eq(buf, pos, id_len, "unique"))
            {
                pos += id_len;
                needs_more = 1;
                continue;
            }

            if (ident_eq(buf, pos, id_len, "is"))
            {
                pos += id_len;
                needs_more = 1;
                continue;
            }

            if (ident_eq(buf, pos, id_len, "extends"))
            {
                pos += id_len;
                needs_more = 1;
                continue;
            }

            if (!needs_more)
            {

                break;
            }

            pos += id_len;
            needs_more = 0;

            size_t after = skip_ws(buf, pos, len);
            if (after < len && buf[after] == '<')
            {
                pos = skip_balanced(buf, after, len, '<', '>');
            }
            continue;
        }

        if (c >= '0' && c <= '9')
        {
            while (pos < len && (is_ident_char(buf[pos]) || buf[pos] == '.'))
                pos++;
            needs_more = 0;
            continue;
        }

        if (c == '-' && pos + 1 < len && buf[pos + 1] >= '0' && buf[pos + 1] <= '9')
        {
            pos++;
            while (pos < len && (is_ident_char(buf[pos]) || buf[pos] == '.'))
                pos++;
            needs_more = 0;
            continue;
        }

        if (c == '?')
        {
            pos++;
            needs_more = 1;
            continue;
        }

        if (c == ':')
        {
            pos++;
            needs_more = 1;
            continue;
        }

        break;
    }

    return pos;
}

static size_t find_prev_token_end(const char *buf, size_t pos)
{
    if (pos == 0)
        return 0;
    size_t p = pos - 1;
    while (p > 0 && (buf[p] == ' ' || buf[p] == '\t' || buf[p] == '\n' || buf[p] == '\r'))
        p--;
    return p;
}

static bool is_postfix_bang(const char *buf, size_t pos)
{
    if (pos == 0)
        return false;
    size_t p = find_prev_token_end(buf, pos);
    char c = buf[p];
    return is_ident_char(c) || c == ')' || c == ']';
}

bool ts_strip_types(const char *ts_src, size_t ts_len,
                    char **out_js, size_t *out_len)
{
    
    char *buf = (char *)malloc(ts_len + 1);
    if (!buf)
        return false;
    memcpy(buf, ts_src, ts_len);
    buf[ts_len] = '\0';

    size_t pos = 0;

    while (pos < ts_len)
    {
        char c = buf[pos];

        if (c == '\'' || c == '"' || c == '`')
        {
            char q = c;
            pos++;
            while (pos < ts_len)
            {
                if (buf[pos] == '\\')
                {
                    pos += 2;
                    continue;
                }
                if (q == '`' && buf[pos] == '$' && pos + 1 < ts_len && buf[pos + 1] == '{')
                {
                    
                    pos = skip_balanced(buf, pos + 1, ts_len, '{', '}');
                    continue;
                }
                if (buf[pos] == q)
                    break;
                pos++;
            }
            if (pos < ts_len)
                pos++;
            continue;
        }

        if (c == '/' && pos + 1 < ts_len && buf[pos + 1] == '/')
        {
            pos += 2;
            while (pos < ts_len && buf[pos] != '\n')
                pos++;
            continue;
        }

        if (c == '/' && pos + 1 < ts_len && buf[pos + 1] == '*')
        {
            pos += 2;
            while (pos + 1 < ts_len && !(buf[pos] == '*' && buf[pos + 1] == '/'))
                pos++;
            if (pos + 1 < ts_len)
                pos += 2;
            continue;
        }

        if (c == '/' && pos > 0)
        {

            size_t pp = find_prev_token_end(buf, pos);
            char prev = buf[pp];
            if (prev == '=' || prev == '(' || prev == ',' || prev == ';' ||
                prev == '!' || prev == '&' || prev == '|' || prev == '?' ||
                prev == ':' || prev == '[' || prev == '{' || prev == '~' ||
                prev == '^' || prev == '%' || prev == '+' || prev == '-' ||
                prev == '*' || prev == '\n' || prev == '\r')
            {
                
                pos++;
                while (pos < ts_len)
                {
                    if (buf[pos] == '\\')
                    {
                        pos += 2;
                        continue;
                    }
                    if (buf[pos] == '[')
                    {
                        
                        pos++;
                        while (pos < ts_len && buf[pos] != ']')
                        {
                            if (buf[pos] == '\\')
                                pos++;
                            pos++;
                        }
                        if (pos < ts_len)
                            pos++;
                        continue;
                    }
                    if (buf[pos] == '/')
                        break;
                    pos++;
                }
                if (pos < ts_len)
                    pos++;
                
                while (pos < ts_len && is_ident_char(buf[pos]))
                    pos++;
                continue;
            }
        }

        if (is_ident_start(c))
        {
            size_t id_start = pos;
            size_t id_len = read_ident(buf, pos, ts_len);
            pos += id_len;

            if (ident_eq(buf, id_start, id_len, "interface"))
            {
                
                size_t erase_start = id_start;
                size_t p = skip_ws(buf, pos, ts_len);
                
                size_t name_len = read_ident(buf, p, ts_len);
                if (name_len > 0)
                    p += name_len;
                p = skip_ws(buf, p, ts_len);
                
                if (p < ts_len && buf[p] == '<')
                    p = skip_balanced(buf, p, ts_len, '<', '>');
                p = skip_ws(buf, p, ts_len);
                
                if (p < ts_len)
                {
                    size_t ext_len = read_ident(buf, p, ts_len);
                    if (ext_len > 0 && ident_eq(buf, p, ext_len, "extends"))
                    {
                        p += ext_len;
                        
                        while (p < ts_len && buf[p] != '{')
                            p++;
                    }
                }
                
                if (p < ts_len && buf[p] == '{')
                    p = skip_balanced(buf, p, ts_len, '{', '}');

                blank(buf, erase_start, p);
                pos = p;
                continue;
            }

            if (ident_eq(buf, id_start, id_len, "type"))
            {
                
                size_t p = skip_ws(buf, pos, ts_len);
                size_t name_len = read_ident(buf, p, ts_len);
                if (name_len > 0)
                {
                    size_t name_end = p + name_len;
                    size_t after_name = skip_ws(buf, name_end, ts_len);
                    
                    bool is_type_alias = false;
                    if (after_name < ts_len)
                    {
                        if (buf[after_name] == '=')
                            is_type_alias = true;
                        else if (buf[after_name] == '<')
                        {
                            size_t after_gen = skip_balanced(buf, after_name, ts_len, '<', '>');
                            size_t after_gen_ws = skip_ws(buf, after_gen, ts_len);
                            if (after_gen_ws < ts_len && buf[after_gen_ws] == '=')
                                is_type_alias = true;
                        }
                    }
                    if (is_type_alias)
                    {
                        size_t erase_start = id_start;
                        
                        size_t p2 = after_name;
                        if (buf[p2] == '<')
                            p2 = skip_balanced(buf, p2, ts_len, '<', '>');
                        p2 = skip_ws(buf, p2, ts_len);
                        if (p2 < ts_len && buf[p2] == '=')
                            p2++;
                        p2 = skip_type_expr(buf, p2, ts_len);
                        
                        p2 = skip_ws(buf, p2, ts_len);
                        if (p2 < ts_len && buf[p2] == ';')
                            p2++;
                        blank(buf, erase_start, p2);
                        pos = p2;
                        continue;
                    }
                }
                
            }

            if (ident_eq(buf, id_start, id_len, "declare"))
            {
                
                size_t erase_start = id_start;
                size_t p = skip_ws(buf, pos, ts_len);
                
                while (p < ts_len)
                {
                    if (buf[p] == ';')
                    {
                        p++;
                        break;
                    }
                    if (buf[p] == '{')
                    {
                        p = skip_balanced(buf, p, ts_len, '{', '}');
                        break;
                    }
                    if (buf[p] == '\'' || buf[p] == '"')
                    {
                        char q = buf[p];
                        p++;
                        while (p < ts_len && buf[p] != q)
                        {
                            if (buf[p] == '\\')
                                p++;
                            p++;
                        }
                        if (p < ts_len)
                            p++;
                        continue;
                    }
                    p++;
                }
                blank(buf, erase_start, p);
                pos = p;
                continue;
            }

            if (ident_eq(buf, id_start, id_len, "public") ||
                ident_eq(buf, id_start, id_len, "private") ||
                ident_eq(buf, id_start, id_len, "protected") ||
                ident_eq(buf, id_start, id_len, "override") ||
                ident_eq(buf, id_start, id_len, "abstract"))
            {

                size_t p = skip_ws(buf, pos, ts_len);
                size_t next_len = read_ident(buf, p, ts_len);
                if (next_len > 0)
                {
                    
                    if (ident_eq(buf, p, next_len, "readonly") ||
                        ident_eq(buf, p, next_len, "static") ||
                        ident_eq(buf, p, next_len, "get") ||
                        ident_eq(buf, p, next_len, "set") ||
                        ident_eq(buf, p, next_len, "async") ||
                        ident_eq(buf, p, next_len, "public") ||
                        ident_eq(buf, p, next_len, "private") ||
                        ident_eq(buf, p, next_len, "protected") ||
                        ident_eq(buf, p, next_len, "override") ||
                        ident_eq(buf, p, next_len, "abstract") ||

                        true)
                    {
                        
                        size_t after = skip_ws(buf, p + next_len, ts_len);
                        if (after < ts_len)
                        {
                            char nc = buf[after];
                            if (nc == '(' || nc == ':' || nc == ',' ||
                                nc == ')' || nc == '=' || nc == ';' ||
                                nc == '<' || nc == '?' || nc == '{' ||
                                nc == '[')
                            {
                                blank(buf, id_start, pos);
                                continue;
                            }
                        }
                    }
                }
                
            }

            if (ident_eq(buf, id_start, id_len, "readonly"))
            {
                
                size_t p = skip_ws(buf, pos, ts_len);
                size_t next_len = read_ident(buf, p, ts_len);
                if (next_len > 0)
                {
                    size_t after = skip_ws(buf, p + next_len, ts_len);
                    if (after < ts_len)
                    {
                        char nc = buf[after];
                        if (nc == ':' || nc == ';' || nc == '=' ||
                            nc == ',' || nc == '?' || nc == ')' ||
                            nc == '[')
                        {
                            blank(buf, id_start, pos);
                            continue;
                        }
                    }
                }
            }

            if (ident_eq(buf, id_start, id_len, "implements"))
            {
                
                size_t erase_start = id_start;
                size_t p = pos;
                while (p < ts_len && buf[p] != '{')
                {
                    if (buf[p] == '<')
                    {
                        p = skip_balanced(buf, p, ts_len, '<', '>');
                        continue;
                    }
                    p++;
                }
                blank(buf, erase_start, p);
                pos = p;
                continue;
            }

            if (ident_eq(buf, id_start, id_len, "as"))
            {

                size_t pp = find_prev_token_end(buf, id_start);
                char prev = buf[pp];
                if (is_ident_char(prev) || prev == ')' || prev == ']' ||
                    prev == '\'' || prev == '"' || prev == '`')
                {
                    size_t erase_start = id_start;
                    size_t p = skip_ws(buf, pos, ts_len);
                    
                    size_t const_len = read_ident(buf, p, ts_len);
                    if (const_len > 0 && ident_eq(buf, p, const_len, "const"))
                    {
                        blank(buf, erase_start, p + const_len);
                        pos = p + const_len;
                        continue;
                    }
                    size_t type_end = skip_type_expr(buf, p, ts_len);
                    blank(buf, erase_start, type_end);
                    pos = type_end;
                    continue;
                }
                
            }

            continue;
        }

        if (c == ':')
        {

            size_t pp = find_prev_token_end(buf, pos);
            char prev = buf[pp];

            if (prev == ')')
            {
                size_t erase_start = pos;
                pos++;
                size_t type_end = skip_type_expr(buf, pos, ts_len);
                
                if (type_end > pos)
                {

                    size_t after = skip_ws(buf, type_end, ts_len);
                    if (after < ts_len)
                    {
                        char nc = buf[after];
                        if (nc == '{' || nc == ';' || nc == ',' ||
                            nc == ')' || nc == ']' ||
                            (nc == '=' && after + 1 < ts_len && buf[after + 1] == '>'))
                        {
                            blank(buf, erase_start, type_end);
                            pos = type_end;
                            continue;
                        }
                    }
                    
                    if (after >= ts_len)
                    {
                        blank(buf, erase_start, type_end);
                        pos = type_end;
                        continue;
                    }
                }
                pos = erase_start + 1; 
                continue;
            }

            if (prev == '?')
            {
                size_t erase_start = pos;
                pos++;
                size_t type_end = skip_type_expr(buf, pos, ts_len);
                if (type_end > pos)
                {
                    blank(buf, erase_start, type_end);
                    pos = type_end;
                    continue;
                }
                pos = erase_start + 1;
                continue;
            }

            if (is_ident_char(prev))
            {
                
                size_t id_beg = pp;
                while (id_beg > 0 && is_ident_char(buf[id_beg - 1]))
                    id_beg--;

                size_t before_id = id_beg;
                while (before_id > 0 && (buf[before_id - 1] == ' ' ||
                                         buf[before_id - 1] == '\t'))
                    before_id--;

                bool is_annotation = false;

                if (before_id > 0)
                {
                    char bc = buf[before_id - 1];
                    if (bc == '(')
                    {
                        
                        is_annotation = true;
                    }
                    else if (bc == ',')
                    {

                        int paren_depth = 0;
                        int brace_depth = 0;
                        int bracket_depth = 0;
                        bool found_context = false;
                        size_t scan = before_id - 1; 
                        while (scan > 0)
                        {
                            scan--;
                            char sc = buf[scan];
                            if (sc == ')')
                                paren_depth++;
                            else if (sc == '(')
                            {
                                if (paren_depth > 0)
                                    paren_depth--;
                                else
                                {
                                    
                                    is_annotation = true;
                                    found_context = true;
                                    break;
                                }
                            }
                            else if (sc == '}')
                                brace_depth++;
                            else if (sc == '{')
                            {
                                if (brace_depth > 0)
                                    brace_depth--;
                                else
                                {
                                    
                                    found_context = true;
                                    break;
                                }
                            }
                            else if (sc == ']')
                                bracket_depth++;
                            else if (sc == '[')
                            {
                                if (bracket_depth > 0)
                                    bracket_depth--;
                                else
                                {
                                    found_context = true;
                                    break;
                                }
                            }
                            
                            else if (sc == '\'' || sc == '"')
                            {
                                char q = sc;
                                while (scan > 0 && buf[scan - 1] != q)
                                    scan--;
                                if (scan > 0)
                                    scan--; 
                            }
                        }
                        (void)found_context;
                        (void)bracket_depth;
                    }
                    else if (is_ident_char(bc))
                    {
                        
                        size_t kw_end = before_id;
                        size_t kw_beg = before_id - 1;
                        while (kw_beg > 0 && is_ident_char(buf[kw_beg - 1]))
                            kw_beg--;
                        size_t kw_len = kw_end - kw_beg;
                        if (ident_eq(buf, kw_beg, kw_len, "let") ||
                            ident_eq(buf, kw_beg, kw_len, "const") ||
                            ident_eq(buf, kw_beg, kw_len, "var") ||
                            ident_eq(buf, kw_beg, kw_len, "function") ||
                            ident_eq(buf, kw_beg, kw_len, "catch"))
                        {
                            is_annotation = true;
                        }
                    }
                    else if (bc == ' ' || bc == '\t')
                    {
                        
                        size_t p2 = before_id - 1;
                        while (p2 > 0 && (buf[p2 - 1] == ' ' || buf[p2 - 1] == '\t'))
                            p2--;
                        if (p2 > 0 && buf[p2 - 1] == '(')
                            is_annotation = true;
                    }
                }

                if (is_annotation)
                {
                    size_t erase_start = pos;
                    pos++;
                    size_t type_end = skip_type_expr(buf, pos, ts_len);
                    if (type_end > pos)
                    {
                        blank(buf, erase_start, type_end);
                        pos = type_end;
                        continue;
                    }
                    pos = erase_start + 1;
                    continue;
                }
            }

            if (prev == ']')
            {
                size_t erase_start = pos;
                pos++;
                size_t type_end = skip_type_expr(buf, pos, ts_len);
                if (type_end > pos)
                {
                    size_t after = skip_ws(buf, type_end, ts_len);
                    if (after < ts_len && (buf[after] == '=' || buf[after] == ';' ||
                                           buf[after] == ',' || buf[after] == ')'))
                    {
                        blank(buf, erase_start, type_end);
                        pos = type_end;
                        continue;
                    }
                }
                pos = erase_start + 1;
                continue;
            }

            pos++;
            continue;
        }

        if (c == '!')
        {
            
            if (is_postfix_bang(buf, pos))
            {

                if (pos + 1 < ts_len && buf[pos + 1] == '=')
                {
                    
                    pos++;
                    continue;
                }
                
                buf[pos] = ' ';
            }
            pos++;
            continue;
        }

        if (c == '<')
        {

            size_t pp = find_prev_token_end(buf, pos);
            if (is_ident_char(buf[pp]))
            {
                
                size_t gen_start = pos;
                size_t gen_end = skip_balanced(buf, pos, ts_len, '<', '>');

                if (gen_end > gen_start + 1 && (gen_end <= ts_len))
                {
                    
                    size_t after = skip_ws(buf, gen_end, ts_len);
                    bool looks_like_generic = false;

                    if (after < ts_len)
                    {
                        char nc = buf[after];

                        if (nc == '(' || nc == '{' || nc == ',' ||
                            nc == ';' || nc == ')' || nc == '=' ||
                            nc == '>' || nc == ':')
                        {
                            looks_like_generic = true;
                        }
                        
                        size_t nid_len = read_ident(buf, after, ts_len);
                        if (nid_len > 0 &&
                            (ident_eq(buf, after, nid_len, "extends") ||
                             ident_eq(buf, after, nid_len, "implements")))
                        {
                            looks_like_generic = true;
                        }
                    }
                    else
                    {
                        looks_like_generic = true; 
                    }

                    if (looks_like_generic)
                    {
                        
                        bool has_arith = false;
                        for (size_t i = gen_start + 1; i < gen_end - 1 && !has_arith; i++)
                        {
                            
                            if (buf[i] == '\'' || buf[i] == '"' || buf[i] == '`')
                            {
                                char q = buf[i];
                                i++;
                                while (i < gen_end - 1 && buf[i] != q)
                                {
                                    if (buf[i] == '\\')
                                        i++;
                                    i++;
                                }
                                continue;
                            }
                            
                            if (buf[i] == '+' || buf[i] == '-' ||
                                buf[i] == '*' || buf[i] == '/' ||
                                buf[i] == '%' || buf[i] == '!' ||
                                buf[i] == '~')
                            {
                                has_arith = true;
                            }
                        }

                        if (!has_arith)
                        {
                            blank(buf, gen_start, gen_end);
                            pos = gen_end;
                            continue;
                        }
                    }
                }
            }
            pos++;
            continue;
        }

        pos++;
    }

    *out_js = buf;
    *out_len = ts_len;
    return true;
}

bool ts_is_typescript(const char *filename)
{
    if (!filename)
        return false;
    size_t len = strlen(filename);
    return len >= 3 && strcmp(filename + len - 3, ".ts") == 0;
}
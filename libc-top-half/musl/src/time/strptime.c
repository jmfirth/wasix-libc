#include <stdlib.h>
#include <langinfo.h>
#include <time.h>
#include <ctype.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

char *strptime(const char *restrict s, const char *restrict f, struct tm *restrict tm)
{
	int i, w, neg, adj, min, range, *dest, dummy;
	const char *ex;
	size_t len;
	int want_century = 0, century = 0, relyear = 0;
	while (*f) {
		if (*f != '%') {
			if (isspace(*f)) for (; *s && isspace(*s); s++);
			else if (*s != *f) return 0;
			else s++;
			f++;
			continue;
		}
		f++;
		if (*f == '+') f++;
		if (isdigit(*f)) {
			char *new_f;
			w=strtoul(f, &new_f, 10);
			f = new_f;
		} else {
			w=-1;
		}
		adj=0;
		switch (*f++) {
		case 'a': case 'A':
			dest = &tm->tm_wday;
			min = ABDAY_1;
			range = 7;
			goto symbolic_range;
		case 'b': case 'B': case 'h':
			dest = &tm->tm_mon;
			min = ABMON_1;
			range = 12;
			goto symbolic_range;
		case 'c':
			s = strptime(s, nl_langinfo(D_T_FMT), tm);
			if (!s) return 0;
			break;
		case 'C':
			dest = &century;
			if (w<0) w=2;
			want_century |= 2;
			goto numeric_digits;
		case 'd': case 'e':
			dest = &tm->tm_mday;
			min = 1;
			range = 31;
			goto numeric_range;
		case 'D':
			s = strptime(s, "%m/%d/%y", tm);
			if (!s) return 0;
			break;
		case 'F':
			/* firebox#RNS: ISO 8601 date (%Y-%m-%d), faithful port of
			 * upstream musl. Use a temp buffer to implement the odd
			 * requirement that the entire field be width-limited but the
			 * year subfield not itself be limited. */
			{
				char tmp[20];
				char *p;
				if (w < 0) w = 10;
				i = 0;
				if (*s == '-' || *s == '+') tmp[i++] = *s++;
				while (*s == '0' && isdigit(s[1])) s++;
				for (; *s && i < w && i + 1 < (int)sizeof tmp; i++)
					tmp[i] = *s++;
				tmp[i] = 0;
				p = strptime(tmp, "%12Y-%m-%d", tm);
				if (!p) return 0;
				s -= tmp + i - p;
			}
			break;
		case 'H':
			dest = &tm->tm_hour;
			min = 0;
			range = 24;
			goto numeric_range;
		case 'I':
			dest = &tm->tm_hour;
			min = 1;
			range = 12;
			goto numeric_range;
		case 'j':
			dest = &tm->tm_yday;
			min = 1;
			range = 366;
			adj = 1;
			goto numeric_range;
		case 'm':
			dest = &tm->tm_mon;
			min = 1;
			range = 12;
			adj = 1;
			goto numeric_range;
		case 'M':
			dest = &tm->tm_min;
			min = 0;
			range = 60;
			goto numeric_range;
		case 'n': case 't':
			for (; *s && isspace(*s); s++);
			break;
		case 'p':
			ex = nl_langinfo(AM_STR);
			len = strlen(ex);
			if (!strncasecmp(s, ex, len)) {
				tm->tm_hour %= 12;
				s += len;
				break;
			}
			ex = nl_langinfo(PM_STR);
			len = strlen(ex);
			if (!strncasecmp(s, ex, len)) {
				tm->tm_hour %= 12;
				tm->tm_hour += 12;
				s += len;
				break;
			}
			return 0;
		case 'r':
			s = strptime(s, nl_langinfo(T_FMT_AMPM), tm);
			if (!s) return 0;
			break;
		case 'R':
			s = strptime(s, "%H:%M", tm);
			if (!s) return 0;
			break;
		case 's':
			/* firebox#59D: seconds since the Epoch. glibc-faithful — POSIX
			 * leaves %s to the implementation; glibc parses the (possibly very
			 * large) count digit-by-digit into a time_t and fills *tm via
			 * localtime_r (which honors TZ). Linux programs — and musl
			 * libc-test functional/strptime ("683078400" %s -> 1991-08-25) —
			 * expect the broken-down fields to be set. Upstream musl parsed and
			 * discarded, leaving tm unchanged: the conformance gap. Like glibc
			 * we require at least one digit and do not accept a sign. */
			{
				time_t __s_secs = 0;
				if (!isdigit(*s)) return 0;
				do {
					__s_secs = __s_secs * 10 + (*s++ - '0');
				} while (isdigit(*s));
				if (!localtime_r(&__s_secs, tm)) return 0;
			}
			break;
		case 'S':
			dest = &tm->tm_sec;
			min = 0;
			range = 61;
			goto numeric_range;
		case 'T':
			s = strptime(s, "%H:%M:%S", tm);
			if (!s) return 0;
			break;
		case 'U':
		case 'W':
			/* Throw away result, for now. (FIXME?) */
			dest = &dummy;
			min = 0;
			range = 54;
			goto numeric_range;
		case 'V':
			/* firebox#RNS: ISO 8601 week number, discarded (upstream musl). */
			dest = &dummy;
			min = 1;
			range = 53;
			goto numeric_range;
		case 'u':
			/* firebox#RNS: ISO weekday, Monday=1 (upstream musl). */
			dest = &tm->tm_wday;
			min = 1;
			range = 7;
			goto numeric_range;
		case 'w':
			dest = &tm->tm_wday;
			min = 0;
			range = 7;
			goto numeric_range;
		case 'z':
			/* firebox#59D: UTC offset. glibc accepts both +HH and +HHMM (and
			 * the '-' forms); upstream musl required exactly 4 digits, which
			 * rejects the 2-digit +HH form that glibc and Linux programs accept
			 * (musl libc-test %z "-06"). Parse the sign, the mandatory HH, then
			 * an optional MM. */
			if (*s == '+') neg = 0;
			else if (*s == '-') neg = 1;
			else return 0;
			s++;
			if (!isdigit(s[0]) || !isdigit(s[1])) return 0;
			tm->__tm_gmtoff = ((s[0]-'0')*10 + (s[1]-'0')) * 3600;
			s += 2;
			if (isdigit(s[0]) && isdigit(s[1])) {
				tm->__tm_gmtoff += ((s[0]-'0')*10 + (s[1]-'0')) * 60;
				s += 2;
			}
			if (neg) tm->__tm_gmtoff = -tm->__tm_gmtoff;
			break;
		case 'x':
			s = strptime(s, nl_langinfo(D_FMT), tm);
			if (!s) return 0;
			break;
		case 'X':
			s = strptime(s, nl_langinfo(T_FMT), tm);
			if (!s) return 0;
			break;
		case 'g':
			/* firebox#RNS: 2-digit ISO year, discarded (upstream musl). */
			dest = &dummy;
			w = 2;
			goto numeric_digits;
		case 'G':
			/* firebox#RNS: 4-digit ISO year, discarded (upstream musl). */
			dest = &dummy;
			if (w<0) w=4;
			goto numeric_digits;
		case 'y':
			dest = &relyear;
			w = 2;
			want_century |= 1;
			goto numeric_digits;
		case 'Y':
			dest = &tm->tm_year;
			if (w<0) w=4;
			adj = 1900;
			want_century = 0;
			goto numeric_digits;
		case '%':
			if (*s++ != '%') return 0;
			break;
		default:
			return 0;
		numeric_range:
			if (!isdigit(*s)) return 0;
			*dest = 0;
			for (i=1; i<=min+range && isdigit(*s); i*=10)
				*dest = *dest * 10 + *s++ - '0';
			if (*dest - min >= (unsigned)range) return 0;
			*dest -= adj;
			switch((char *)dest - (char *)tm) {
			case offsetof(struct tm, tm_yday):
				;
			}
			goto update;
		numeric_digits:
			neg = 0;
			if (*s == '+') s++;
			else if (*s == '-') neg=1, s++;
			if (!isdigit(*s)) return 0;
			for (*dest=i=0; i<w && isdigit(*s); i++)
				*dest = *dest * 10 + *s++ - '0';
			if (neg) *dest = -*dest;
			*dest -= adj;
			goto update;
		symbolic_range:
			for (i=2*range-1; i>=0; i--) {
				ex = nl_langinfo(min+i);
				len = strlen(ex);
				if (strncasecmp(s, ex, len)) continue;
				s += len;
				*dest = i % range;
				break;
			}
			if (i<0) return 0;
			goto update;
		update:
			//FIXME
			;
		}
	}
	if (want_century) {
		tm->tm_year = relyear;
		if (want_century & 2) tm->tm_year += century * 100 - 1900;
		else if (tm->tm_year <= 68) tm->tm_year += 100;
	}
	return (char *)s;
}

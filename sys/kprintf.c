#include <sys/kprintf.h>
#include <stdarg.h>
#include <sys/defs.h>

static volatile char *video = (volatile char*)0xB8000;
static long data_written = 0;

void flushtime(int seconds)
{
	char* timeArea = (char*)0xb8000+160*24 + 140;
	char str[1024]; int i = 0;
	while(seconds){
		str[i++] = (seconds%10) + '0';
		seconds/=10;
	}
	str[i] = '\0';
	
	//reverse
	int j = 0; i--;
	while(j < i)
	{
		char temp = str[i];
		str[i] = str[j];
		str[j] = temp;
		j++;i--;
	}

   	int colour = 7; int k = 0;
    while( str[k] != '\0' && str[k] != '\n')
    {
        *timeArea++ = str[k++];
        *timeArea++ = colour;
    }	
}

void checkForScroll(){

	return;

	if (data_written >= 80*25)
	{
		//Scroll
		char* oldLocation = (char*)0xb80a0; char* temp2;
		for(temp2 = (char*)0xb8000; temp2 < (char*)0xb8000+160*24; temp2 += 2){
			*temp2 = *oldLocation;
			*(temp2+1) = 7;
			oldLocation+=2;
		}

		video = temp2;
		data_written -= 80;
		
		temp2++;
		while(temp2 < (char*)0xb8000+160*24){
			*temp2 = 7;
			temp2+=2;
		}
	}
}

void flush(const char* text)
{
	checkForScroll();
   	int colour = 7;
    while( *text != 0 && *text != '\n')
    {
        *video++ = *text++;
        *video++ = colour;
        data_written++;
    }	
    if (*text == '\n')
    {
 		while(data_written%80 != 0){
			video+=2;
			data_written++;
		}   	
    }
}

void flushchar(const char ch)
{
	checkForScroll();
    *video++ = ch;
    *video++ = 7;
    data_written++;
}

void flushint(int text)
{
	char str[1024]; int i = 0;
	while(text){
		str[i++] = (text%10) + '0';
		text/=10;
	}
	str[i] = '\0';
	
	//reverse
	int j = 0; i--;
	while(j < i)
	{
		char temp = str[i];
		str[i] = str[j];
		str[j] = temp;
		j++;i--;
	}

	flush(str);
}

void flushhex(uint64_t num)
{
	char ret[1024];
	int r = 0;

	do
	{
		int rem = num%16;
		if (rem >= 10)
		{
			ret[r++] = (char)('a' + rem-10);
		}
		else
		{
			ret[r++] = rem + '0';
		}
		num/=16;
	}while(num);
	ret[r] = '\0';

	//reverse
	r--;
	int i = 0;
	while(i < r)
	{
		char temp = ret[i];
		ret[i] = ret[r];
		ret[r] = temp;
		i++;r--;		
	}

	flush("0x");
	flush(ret);

}

void flushNewLine(){
	checkForScroll();
	while(data_written%80 != 0){
		*(video+1) = 7;
		video+=2;
		data_written++;
	}
}

void kprintf(const char *fmt, ...)
{
	va_list ap;
    int d;
    char *s, c;
	uint64_t address;

   va_start(ap, fmt);
    while (*fmt){
    	char ch = (char)*fmt;
    	char nextch = (*(fmt+1))?(char)*(fmt+1):'\0';
    	if (ch == '\n')
    	{
    		flushNewLine();
    	}
    	else if (ch == '%' && nextch == 's')
    	{
            s = va_arg(ap, char *);
            flush(s);
            fmt++;
    	}
    	else if (ch == '%' && nextch == 'c')
    	{
            c = (char) va_arg(ap, int);
            flushchar(c);
            fmt++;
    	}
    	else if (ch == '%' && nextch == 'd')
    	{
            d = va_arg(ap, int);
            flushint(d);
            fmt++;
    	}
    	else if (ch == '%' && nextch == 'x')
    	{
            d = va_arg(ap, int);
            flushhex(d);
            fmt++;
    	}
    	else if (ch == '%' && nextch == 'p')
    	{
            address = va_arg(ap, uint64_t);
            flushhex(address);
            fmt++;
    	}
    	else
    	{
    		flushchar(ch);
    	}
 
       	fmt++;
    }
    va_end(ap);
}

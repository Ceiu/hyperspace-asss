#define PARAM_BUFFER 64
typedef enum {
	cm_regular,
	cm_escape,
	cm_switch,
} getParamMode_t;
/*
	getParam(char *params, char key, char *buffer)
	- Takes paramaters to a command and gets info on certain switches/options
	- if key is K, return the X values from parts of params like "-K=XXX"
	- if key is 0, return all characters not part of params like "-X=YYY" */ char *getParam(const char *params, char key, char *buffer) {
	char activekey = 0;
	getParamMode_t mode = cm_regular;
	int i = 0;
	char activechar = *params;
	char lastchar = ' ';
	int keybuf = 0;
	while (activechar)
	{
		switch(mode)
		{
		case cm_regular:
			if ((activechar == '-') && (lastchar == ' '))
			{
				mode = cm_switch;
				break;
			}
			if ((activechar == '=') && (!keybuf))
			{
				break;
			}
			if (activechar == ' ')
			{
				if (activekey)
				{
					activekey = 0;
					keybuf = 0;
					mode = cm_regular;
					break;
				}
			}
			else if (activechar == '\\')
			{
				mode = cm_escape;
				break;
			}
			else if ((activechar == '_') && activekey) //allow -a=bc_de
			{
				activechar = ' ';
			}
			//No break here, because we want to still write to the buffer if we haven't broken yet
			//cm_escape just bypasses all of these checks on special characters
		case cm_escape:
			if ((activekey == key) && buffer)
			{
				buffer[i++] = activechar;
			}
			++keybuf;
			mode = cm_regular;
			break;
		case cm_switch:
			activekey = activechar;
			keybuf = 0;
			mode = cm_regular;
			break;
		}
		lastchar = *params;
		params += 1;
		activechar = *params;
		//stop reading after we read too many bytes.
		if (i >= PARAM_BUFFER - 1)
		{
			break;
		}
	}
	if (buffer)
		buffer[i] = 0;
	return buffer;
}
/*
	getBoolParam(char *params, char key)
	- Returns true if the specified key switch is found */ int getBoolParam(const char *params, char key) {
	char activekey = 0;
	getParamMode_t mode = cm_regular;
	char activechar = *params;
	char lastchar = ' ';
	while (activechar)
	{
		switch(mode)
		{
		case cm_regular:
			if ((activechar == '-') && (lastchar == ' '))
			{
				mode = cm_switch;
				break;
			}
			if (activechar == '=')
			{
				break;
			}
			if (activechar == ' ')
			{
				if (activekey)
				{
					activekey = 0;
					mode = cm_regular;
					break;
				}
			}
			else if (activechar == '\\')
			{
				mode = cm_escape;
				break;
			}
			else if ((activechar == '_') && activekey)
			{
				activechar = ' ';
			}
			//No break here, because we want to still write to the buffer if we haven't broken yet
			//cm_escape just bypasses all of these checks on special characters
		case cm_escape:
			mode = cm_regular;
			break;
		case cm_switch:
			if (activechar == key)
				return 1;
			activekey = activechar;
			mode = cm_regular;
			break;
		}
		params += 1;
		activechar = *params;
	}
	return 0;
}
/*
	getIntParam(char *params, char key)
	- Takes paramaters to a command and gets info on certain integers switches/options
	- if key is K, return the X values from parts of params like "-K=XXX"
	- if key is 0, return all characters not part of params like "-X=YYY" */ int getIntParam(const char *params, char key) {
	char buffer[PARAM_BUFFER];
	int result;
	getParam(params, key, buffer);
	result = atoi(buffer);
	return result;
}
/*
	formatTime(int seconds, char *buffer)
	- Changes a time in seconds to an H:MM:SS format
	- buffer should be at least 14 bytes large to account for all possible values of seconds */ char *formatTime(int seconds, char *buffer) {
	int minutes = seconds / 60;
	int hours = minutes / 60;
	minutes = minutes % 60;
	seconds = seconds % 60;
	
	if (!hours)
	{
		if (seconds < 10)
			sprintf(buffer, "%i:0%i", minutes, seconds);
		else
			sprintf(buffer, "%i:%i", minutes, seconds);
	}
	else
	{
		if (minutes < 10)
		{
			if (seconds < 10)
				sprintf(buffer, "%i:0%i:0%i", hours, minutes, seconds);
			else
				sprintf(buffer, "%i:0%i:%i", hours, minutes, seconds);
		}
		else
		{
			if (seconds < 10)
				sprintf(buffer, "%i:%i:0%i", hours, minutes, seconds);
			else
				sprintf(buffer, "%i:%i:%i", hours, minutes, seconds);
		}
	}
	return buffer;
}

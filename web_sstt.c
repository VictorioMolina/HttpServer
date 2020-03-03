#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdbool.h>
#include <regex.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/sendfile.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define VERSION 24 // TODO - ¿Para qué sirve?
#define BUFSIZE 8096
#define ERROR 42
#define LOG 44

/* HTTP STATUS CODES */
#define OK 200
#define BADREQUEST 400
#define FORBIDDEN 403
#define NOTFOUND 404
#define METHODNOTALLOWED 405
#define UNSUPPORTEDMEDIATYPE 415
#define HTTPVERSIONNOTSUPPORTED 505

struct
{
	char *ext;
	char *filetype;
} extensions[] = {
	{"gif", "image/gif"},
	{"jpg", "image/jpg"},
	{"jpeg", "image/jpeg"},
	{"png", "image/png"},
	{"ico", "image/ico"},
	{"zip", "image/zip"},
	{"gz", "image/gz"},
	{"tar", "image/tar"},
	{"htm", "text/html"},
	{"html", "text/html"},
	{"mp4", "video/mp4"},
	{"css", "text/css"},
	//{0,0}
};

void debug(int log_message_type, char *message, char *additional_info, int socket_fd)
{
	// TODO - Devolver el código HTML adaptado a cada error
	int fd;
	char logbuffer[BUFSIZE * 2];

	switch (log_message_type)
	{
	case ERROR:
		(void)sprintf(logbuffer, "ERROR: %s:%s Errno=%d exiting pid=%d\n", message, additional_info, errno, getpid());
		break;
	case BADREQUEST:
		// Enviar como respuesta 400 BAD REQUEST
		(void)sprintf(logbuffer, "BAD REQUEST: %s:%s\n", message, additional_info);
		break;
	case FORBIDDEN:
		// Enviar como respuesta 403 Forbidden
		(void)sprintf(logbuffer, "FORBIDDEN: %s:%s", message, additional_info);
		break;
	case NOTFOUND:
		// Enviar como respuesta 404 Not Found
		(void)sprintf(logbuffer, "NOT FOUND: %s:%s\n", message, additional_info);
		break;
	case METHODNOTALLOWED:
		// Enviar como respuesta 405 Method Not Allowed
		(void)sprintf(logbuffer, "METHOD NOT ALLOWED: %s:%s\n", message, additional_info);
		break;
	case UNSUPPORTEDMEDIATYPE:
		// Enviar como respuesta 415 Unsupported Media Type
		(void)sprintf(logbuffer, "UNSUPPORTED MEDIA TYPE: %s:%s\n", message, additional_info);
		break;
	case HTTPVERSIONNOTSUPPORTED:
		// Enviar como respuesta 505 HTTP Version Not Supported
		(void)sprintf(logbuffer, "HTTP VERSION NOT SUPPORTED: %s:%s\n", message, additional_info);
		break;
	case LOG:
		(void)sprintf(logbuffer, "INFO: %s:%s:%d\n", message, additional_info, socket_fd);
		break;
	}

	if ((fd = open("webserver.log", O_CREAT | O_WRONLY | O_APPEND, 0644)) >= 0)
	{
		(void)write(fd, logbuffer, strlen(logbuffer));
		(void)write(fd, "\n", 1);
		(void)close(fd);
	}

	// TODO - ¿es correcto? Preguntar al profesor
	if (log_message_type == ERROR || log_message_type == NOTFOUND || log_message_type == FORBIDDEN ||
		log_message_type == HTTPVERSIONNOTSUPPORTED || log_message_type == BADREQUEST ||
		log_message_type == METHODNOTALLOWED)
	{
		exit(3);
	}
}

// Función auxiliar para reconocer expresiones regulares
bool match(const char *str, const char *pattern)
{
	regex_t regex;
	if (regcomp(&regex, pattern, REG_EXTENDED) != 0)
	{
		fprintf(stderr, "No se ha podido compilar la expresion regular\n");
		return false;
	}

	int result = regexec(&regex, str, 0, NULL, 0);
	regfree(&regex);
	if (result == REG_NOMATCH)
	{
		return false;
	}
	return true;
}

// Función auxiliar para obtener el tipo de fichero
char *getFileType(char *url)
{
	char *ptr;
	// Obtenemos la extension del archivo solicitado
	if ((ptr = strrchr(url, '.')) == NULL)
	{
		return NULL;
	}

	const char *ext = ptr + 1;
	if (!ext)
	{
		return NULL;
	}

	// Buscamos el MIME asociado
	for (int i = 0; i < sizeof(extensions) / sizeof(extensions[0]); i++)
	{
		if (strcmp(extensions[i].ext, ext) == 0)
		{
			return extensions[i].filetype;
		}
	}

	// Extension no compatible
	return NULL;
}

// Función auxiliar para calcular el tamaño de un fichero
unsigned long fsize(char *url)
{
	FILE *file = fopen(url, "r");

	fseek(file, 0, SEEK_END);
	unsigned long length = (unsigned long)ftell(file);

	fclose(file);
	return length;
}

// Función auxiliar para construir las cabeceras HTTP
char *buildHeader(int status_code, char *message, const char *filetype, unsigned long file_size)
{
	char *header = (char *)malloc(BUFSIZE);

	char *connection = "Connection: close\r\n";
	char *keep_alive = "";

	// Para HTTP/1.1 implementamos la persistencia
	if (status_code == OK)
	{
		connection = "Connection: Keep-Alive\r\n";
		keep_alive = "Keep-Alive: timeout=5, max=1000\r\n";
	}

	sprintf(header, "HTTP/1.1 %d %s\r\n"
					"Content-Length: %ld\r\n"
					"%s"
					"Content-Type: %s; charset=UTF-8\r\n"
					"%s"
					"\r\n",
			status_code, message, file_size, connection, filetype, keep_alive);

	return header;
}

// Función auxiliar para construir el esqueleto HTML de las páginas de error
char *buildErrorPage(int status_code, char *message)
{
	char *data;
	asprintf(&data, "<!DOCTYPE html>\n"
					"<html>\n\n"
					"<head>\n"
					"\t<link rel='icon' type='image/ico' href='favicon.ico'>\n"
					"\t<link rel='stylesheet' type='text/css' href='/styles.css'>\n"
					"\t<meta charset='UTF-8'>\n"
					"\t<title>Servicios Telemáticos</title>\n"
					"</head>\n\n"
					"<body>\n"
					"\t<div>\n"
					"\t\t<h1>ERROR %d</h1>\n"
					"\t\t<h2>%s</h2>\n"
					"\t\t<img src='/index_files/error.jpg' />\n"
					"\t</div>\n"
					"</body>\n\n"
					"</html>",
			 status_code, message);

	return data;
}

// Función auxiliar para enviar la respuesta HTTP de los errores
void sendErrorResponse(int status_code, char *message, int socket_fd)
{
	// Código HTML de la página de error
	char *data = buildErrorPage(status_code, message);

	// Construimos la cabecera de la respuesta HTTP
	char *header = buildHeader(status_code, message, "text/html", strlen(data));

	// Construimos la respuesta
	char *response = (char *)malloc(BUFSIZE);
	sprintf(response, "%s"
					  "%s",
			header, data);

	// Enviamos la respuesta de error
	write(socket_fd, response, strlen(response));

	free(response);
	free(header);
	free(data);

	exit(1);
}

// Función auxiliar para cerrar la conexión
void closeConnection(int descriptorFichero)
{
	// TODO - Cerramos la conexión
	char response[BUFSIZE];

	write(descriptorFichero, response, strlen(response));
}

// Función auxiliar para comprobar si el fichero es un directorio existente
bool isDirectory(const char *path)
{
	struct stat statbuf;
	if (stat(path, &statbuf) != 0)
		return 0;
	return S_ISDIR(statbuf.st_mode);
}

// Funcion auxiliar para redirigir a URLs con Trailing Slash
void redirectTrailingSlash(char *url, int socket_fd)
{
	char *response = (char *)malloc(BUFSIZE);

	sprintf(response, "HTTP/1.1 301 Moved Permanently\r\n"
					  "Location: %s/\r\n"
					  "Content-Length: 0\r\n"
					  "Content-Type: text/html\r\n"
					  "\r\n",
			url);

	write(socket_fd, response, strlen(response));

	free(response);
	exit(1);
}

// Función auxiliar que lista el contenido de un directorio
void listDirectoryContent(char *url, int socket_fd)
{
	DIR *dir;
	struct dirent *ent;

	// Si el directorio existe...
	if ((dir = opendir(url)) != NULL)
	{
		// Construimos el código HTML con el contenido
		char *data;
		asprintf(&data, "<!DOCTYPE html>\n"
						"<html>\n\n"
						"<head>\n"
						"\t<link rel='icon' type='image/ico' href='favicon.ico'>\n"
						"\t<meta charset='UTF-8' />"
						"\t<title>Index of %s</title>\n"
						"</head>\n\n"
						"<body>\n"
						"<h1>Index of %s</h1>"
						"\t<table>\n",
				 url, url);

		while ((ent = readdir(dir)) != NULL)
		{
			// Icono correspondiente a cada tipo de entrada
			char *icon_path =
				(ent->d_type == DT_DIR) ? "/icons/dir.png" : "/icons/regfile.png";

			asprintf(&data, "%s"
							"\t\t<tr>\n"
							"\t\t\t<td valign='top'><img src='%s' /></td>\n",
					 data, icon_path);

			// Enlace al contenido
			asprintf(&data, "%s"
							"\t\t\t<td><a href='%s'>%s</a></td>\n"
							"\t\t</tr>\n",
					 data, ent->d_name, ent->d_name);
		}

		asprintf(&data, "%s"
						"\t</table>\n"
						"</body>\n\n"
						"</html>\n",
				 data);

		// Construimos la cabecera de la respuesta HTTP
		char *header = buildHeader(OK, "OK", "text/html", strlen(data));

		// Construimos la respuesta
		char *response = (char *)malloc(BUFSIZE);
		sprintf(response, "%s"
						  "%s",
				header, data);

		// Enviamos la respuesta con el listado del directorio
		write(socket_fd, response, strlen(response));

		free(response);
		free(header);
		free(data);

		// Enviamos la respuesta
		closedir(dir);
		exit(1);
	}

	exit(3);
}

// Función encargada de procesar cada petición web
void process_web_request(int descriptorFichero)
{
	// Definimos buffer y variables necesarias para leer las peticiones
	char buffer[BUFSIZE] = {0};
	fd_set rfds;
	struct timeval tv;
	int retval;
	char *method, *url, *version;

	// Observamos la entrada del descriptor de fichero
	FD_ZERO(&rfds);
	FD_SET(descriptorFichero, &rfds);

	// Esperar hata 5 segundos
	tv.tv_sec = 5;
	tv.tv_usec = 0;
	retval = select(descriptorFichero + 1, &rfds, NULL, NULL, &tv);

	if (retval)
	{
		int bytes_leidos = read(descriptorFichero, buffer, BUFSIZE);
		debug(LOG, "request content", buffer, descriptorFichero);
	}
	else
	{
		debug(LOG, "Conexion cerrada", "No ha habido ningún dato en 5 segundos", descriptorFichero);
		closeConnection(descriptorFichero);
	}

	// Comprobación de errores de lectura
	// Es preciso hacer una copia del buffer ya que strtok es una función destructiva
	char *_buffer = (char *)malloc(BUFSIZE);

	strcpy(_buffer, buffer);

	bool check_request_line = true;
	char *token_line = strtok(_buffer, "\r\n");
	while (token_line != NULL)
	{
		(void)printf("%s\n", token_line);
		// Tratamos la linea de solicitud
		if (check_request_line)
		{
			// Obtenemos los campos de la linea de solicitud y los validamos
			char *field;
			int field_position = 0;
			while ((field = strsep(&token_line, " ")) != NULL)
			{
				switch (field_position)
				{
				case 0:
					// En esta posición se encuentra el método HTTP
					method = strdup(field);

					// Si el método no es GET se produce un error
					if (strcmp(method, "GET") != 0)
					{
						sendErrorResponse(METHODNOTALLOWED, "Method Not Allowed", descriptorFichero);
						debug(METHODNOTALLOWED, "Metodo no permitido", "El servidor solo permite el método GET", descriptorFichero);
					}
					break;

				case 1:
					// En esta posición se encuentra la URL solicitada
					url = strdup(field);

					// Ignoramos el caracter '/' de la URL
					memmove(url, url + 1, strlen(url) + 1);
					break;

				case 2:
					// En esta posición se encuentra la versión de HTTP
					version = strdup(field);

					// Si la versión del protocolo no es la 1.1 se produce un error
					if (strcmp(version, "HTTP/1.1") != 0)
					{
						sendErrorResponse(HTTPVERSIONNOTSUPPORTED, "HTTP Version Not Supported", descriptorFichero);
						debug(HTTPVERSIONNOTSUPPORTED, "Version HTTP no soportada", "Solo se soporta la versión 1.1 de HTTP", descriptorFichero);
					}
					break;

				default:
					sendErrorResponse(BADREQUEST, "Bad Request", descriptorFichero);
					debug(BADREQUEST, "El servidor no puede procesar la petición debido a un error en el formato de la linea de solicitud",
						  "Formato:\t 'METHOD URL HTTP_VERSION\\r\\n'", descriptorFichero);
				}

				field_position += 1;
			}

			check_request_line = false;
		}
		else
		{
			// Tratamos las lineas de cabecera
			if (!match(token_line, "([a-zA-Z-]+): (.*)"))
			{
				sendErrorResponse(BADREQUEST, "Bad Request", descriptorFichero);
				debug(BADREQUEST, "El formato de las cabeceras no es valido", "Formato:\t 'key: value\\r\\n'", descriptorFichero);
			}
		}

		token_line = strtok(NULL, "\r\n");
	}
	free(_buffer);

	// Si la lectura tiene datos válidos terminar el buffer con un \0
	buffer[strlen(buffer) + 1] = '\0';

	// Se eliminan los caracteres de retorno de carro y nueva linea
	char *start = NULL, *end = NULL;
	char delete[] = "\r\n";
	while ((start = strstr(buffer, delete)) != NULL)
	{
		end = start + strlen(delete);
		memmove(start, start + strlen(delete), strlen(end) + 1);
	}

	//	TODO - Creo que ya está hecho
	//	TRATAR LOS CASOS DE LOS DIFERENTES METODOS QUE SE USAN
	//	(Se soporta solo GET)
	//

	/*
		Prohibimos el acceso ilegal a directorios superiores de la jerarquia 
		de directorios del sistema
	*/

	if (match(url, "^/.*$") || match(url, "^[.][.]*$"))
	{
		sendErrorResponse(FORBIDDEN, "Forbidden", descriptorFichero);
	}

	// Cuando el GET es para / devolvemos /index.html
	if (strcmp(url, "") == 0)
	{
		url = strdup("index.html");
	}

	/* Comprobamos que el fichero solicitado exista en el servidor */
	int file;
	char *filetype;

	// Si el fichero solicitado es un directorio existente...
	if (isDirectory(url))
	{
		/* 
			Si la URL no tiene 'Trailing Slash', redirigimos a una URL
			que si lo tenga para evitar contenido duplicado.
		*/
		if (!match(url, "^(.)*/$"))
		{
			redirectTrailingSlash(url, descriptorFichero);
		}
		// Listamos el contenido del directorio
		listDirectoryContent(url, descriptorFichero);
	}
	else
	{
		/*
			En caso de que no sea un directorio existente,
			comprobamos que el fichero dado al menos exista... 
		*/
		if ((file = open(url, O_RDONLY)) == -1)
		{
			sendErrorResponse(NOTFOUND, "Not Found", descriptorFichero);
			debug(NOTFOUND, "La URL solicitada no se encuentra en el servidor", "URL incorrecta", descriptorFichero);
		}
		/*
			En caso de que exista, evaluamos el tipo de fichero que se está solicitando
			y si no está soportado devolvemos el error correspondiente
		*/
		filetype = getFileType(url);
		if (filetype == NULL)
		{
			sendErrorResponse(UNSUPPORTEDMEDIATYPE, "Unsupported Media Type", descriptorFichero);
			debug(UNSUPPORTEDMEDIATYPE, "Tipo de archivo no soportado", "Pruebe con otro tipo de fichero", descriptorFichero);
		}
	}

	// Obtenemos la longitud del archivo solicitado
	unsigned long length = fsize(url);

	// Construimos la cabecera de la respuesta HTTP
	char *header = (char *)malloc(BUFSIZE);
	header = buildHeader(OK, "OK", filetype, length);

	/*
		En caso de que el fichero sea soportado, exista, etc. se envia el fichero con la cabecera
		correspondiente, y el envio del fichero se hace en bloques de un máximo de 8kB
	*/
	write(descriptorFichero, header, strlen(header)); // Enviamos el header
	free(header);

	// Enviamos el fichero solicitado en bloques de 8K
	int nsegmentos = length / BUFSIZE;
	off_t *offset = NULL;
	for (int i = 0; i < nsegmentos; i++)
	{
		sendfile(descriptorFichero, file, offset, BUFSIZE);
		// (void)printf("Enviados 8K de datos\n");
		// sleep(3);
	}
	// Enviamos el resto de bytes del fichero
	int resto = length % BUFSIZE;
	sendfile(descriptorFichero, file, offset, resto);
	// (void)printf("Enviados los %dK restantes", resto);

	close(file);
	close(descriptorFichero);

	// Evitamos fuga de memoria
	free(method);
	free(url);
	free(version);

	exit(1);
}

int main(int argc, char **argv)
{
	int i, port, pid, listenfd, socketfd;
	socklen_t length;
	static struct sockaddr_in cli_addr;  // static = Inicializado con ceros
	static struct sockaddr_in serv_addr; // static = Inicializado con ceros

	//  Argumentos que se esperan:
	//
	//	argv[1]
	//	En el primer argumento del programa se espera el puerto en el que el servidor escuchara
	//
	//  argv[2]
	//  En el segundo argumento del programa se espera el directorio en el que se encuentran los ficheros del servidor

	/* Verficamos que los argumentos que se pasan al iniciar el programa son los esperados */
	if (!argv[1] || !argv[2])
	{
		(void)printf("ERROR: No se han introducido todos los argumentos\n");
		exit(EXIT_FAILURE);
	}

	if (atoi(argv[1]) < 0)
	{
		(void)printf("ERROR: El puerto introducido no es correcto\n");
		exit(EXIT_FAILURE);
	}

	/*
		Verficamos que el directorio escogido es apto. Que no es un directorio del sistema y que se tienen
		permisos para ser usado
	*/
	if (chdir(argv[2]) == -1)
	{
		(void)printf("ERROR: No se puede cambiar de directorio %s\n", argv[2]);
		exit(4);
	}

	// Hacemos que el proceso sea un demonio sin hijos zombies
	if (fork() != 0)
		return 0; // El proceso padre devuelve un OK al shell

	(void)signal(SIGCHLD, SIG_IGN); // Ignoramos a los hijos
	(void)signal(SIGHUP, SIG_IGN);  // Ignoramos cuelgues

	debug(LOG, "web server starting...", argv[1], getpid());

	// Preparamos el socket de red
	if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
		debug(ERROR, "system call", "socket", 0);

	port = atoi(argv[1]);

	if (port < 0 || port > 60000)
		debug(ERROR, "Puerto invalido, prueba un puerto de 1 a 60000", argv[1], 0);

	// Se crea una estructura para la información IP y puerto donde escucha el servidor
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = htonl(INADDR_ANY); // Escucha en cualquier IP disponible
	serv_addr.sin_port = htons(port);			   // ... en el puerto port especificado como parámetro

	if (bind(listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
	{
		perror("ERROR");
		debug(ERROR, "system call", "bind", 0);
	}

	if (listen(listenfd, 64) < 0)
		debug(ERROR, "system call", "listen", 0);

	while (1)
	{
		length = sizeof(cli_addr);
		if ((socketfd = accept(listenfd, (struct sockaddr *)&cli_addr, &length)) < 0)
			debug(ERROR, "system call", "accept", 0);

		if ((pid = fork()) < 0)
		{
			debug(ERROR, "system call", "fork", 0);
		}
		else
		{
			if (pid == 0)
			{ // Proceso hijo
				(void)close(listenfd);
				process_web_request(socketfd); // El hijo termina tras llamar a esta función
			}
			else
			{ // Proceso padre
				(void)close(socketfd);
			}
		}
	}
}

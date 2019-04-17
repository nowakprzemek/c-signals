#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h> 
#include <signal.h>
#include <sys/shm.h>
#include <sys/types.h>

#define TRYB_INTERAKTYWNY 		'1'
#define TRYB_PRACY_Z_PLIKIEM	'2'
#define TRYB_URANDOM 			'3'
#define NAZWA_FIFO				"fifo"

//--------------------------------------------------------------//
//	Sygnaly uzywane w programie:								
//																
//	sygnal zakonczenia dzialania (S1) - SIGINT					
//  sygnal wstrzymania dzialania (S2) - SIGUSR1					
//	sygnal wznawienia dzialanie  (S3) - SIGCONT					
//																
// oprocz pipe i fifo program korzysta tez z pamieci wspoldzielonej przy obsludze sygnalow
// w pamieci wspoldzielonej znajduje sie tablica z pidami procesow(pidyProcesowPotomnych) 
// i miejsce na kod sygnalu(kodOtrzymanegoSygnalu)
//
// obsluga sygnalow wyglada nastepujaco:
// proces, ktory otrzymal sygnal zapisuje kod otrzymanego sygnalu do pamieci wspoldzielonej
// i wysyla do 2 pozostalych procesow sygnal SIGUSR2, a nastepnie sam obsluguje dany sygnal
// pozostale 2 procesy po otrzymaniu SIGUSR2 odczytuja kod sygnalu z pamieci i obsluguja go
// nie mozna po prostu wyslac otrzymanego sygnalu do pozostalych procesow
// bo wtedy tamte znowu wyslaly by do pozostalych (w tym tego, ktory oryginalne otrzymal sygnal) i tak w nieskonczonosc
//
// wstrzymanie/wznowienie dzialania programu polega na przelaczeniu wartosci zmiennej czyProgramWstrzymany
// jezeli ta zmienna ma wartosc 1 to program nic nie robi

pid_t *pidyProcesowPotomnych;
int *kodOtrzymanegoSygnalu;
int czyProgramWstrzymany = 0;
int trybPracy;

FILE * wybierzTrybPracy(void);
FILE * zapytajUzytkownikaOTrybPracy(void);
FILE * otworzPlikOdUzytkownika(void);
FILE * otworzURandom(void);
FILE * otworzPlikDoOdczytu(char *);
void wyswietlInstrukcje(void);
void zamienBajtNaHex(unsigned char, char *);
int utworzProcesyPotomne(void);
void wykonajKodPierwszegoProcesu(FILE *, int *);
void wykonajKodDrugiegoProcesu(int *);
void wykonajKodTrzeciegoProcesu(void);
void wykonajKodProcesuMacierzystego(void);
void czekajNaZakonczenieProcesowPotomnych(void);
void posprzatajPoProgramie(void);
void * utworzPamiecWspoldzielona(int rozmiar);
void odbierzSygnal(int kodSygnalu);
void poinformujPozostaleProcesy(int kodSygnalu);
void odbierzSIGUSR2(int signum);
void obsluzSygnal(int kodSygnalu);
void zarejestrujSygnaly(void);

int main(int argc, char** argv)
{
	zarejestrujSygnaly();
	wyswietlInstrukcje();
	FILE *zrodloDanych = wybierzTrybPracy();
	
	// utworzenie fifo, pipe i pamieci wspoldzielonej	
	mkfifo(NAZWA_FIFO, 0600);
	int pipe1_2[2];
	pipe(pipe1_2);
	pidyProcesowPotomnych = (pid_t *)utworzPamiecWspoldzielona(3 * sizeof(pid_t));
	kodOtrzymanegoSygnalu = (int *) utworzPamiecWspoldzielona(sizeof(int));
	
	int numerProcesu = utworzProcesyPotomne();
	switch (numerProcesu)
	{
		case 0 :
			wykonajKodPierwszegoProcesu(zrodloDanych, pipe1_2);
			break;
		case 1 :
			wykonajKodDrugiegoProcesu(pipe1_2);
			break;
		case 2 :
			wykonajKodTrzeciegoProcesu();
			break;
		case 3 :
			wykonajKodProcesuMacierzystego();
			break;
	}
	
	return 0;
}

FILE * wybierzTrybPracy(void)
{
	FILE *zrodloDanych = zapytajUzytkownikaOTrybPracy();
	
	// petla na wypadek, gdyby nie udalo sie wybrac poprawnego zrodla danych
	// np. uzytkownik wprowadzil numer, ktory nie odpowiada zadnemu trybowi
	// albo z jakiegos powodu nie udalo sie oworzyc wskazanego pliku
	// pytamy uzytkownika az do skutku
	while (zrodloDanych <= 0)
	{
		zrodloDanych = zapytajUzytkownikaOTrybPracy();
	}
	
	return zrodloDanych;
}

FILE * zapytajUzytkownikaOTrybPracy(void)
{
	printf("Wybierz tryb pracy (podaj numer trybu):\n");
	
	char wybranyTryb;
	scanf(" %c", &wybranyTryb);
	FILE *wybraneZrodlo;
	
	switch (wybranyTryb)
	{
		case TRYB_INTERAKTYWNY:
			wybraneZrodlo = stdin;
			trybPracy = 1;
			break;
		case TRYB_PRACY_Z_PLIKIEM:
			wybraneZrodlo = otworzPlikOdUzytkownika();
			trybPracy = 2;
			break;
		case TRYB_URANDOM:
			wybraneZrodlo = otworzURandom();
			trybPracy = 3;
			break;
		default:
			// uzytkownik wprowadzil cyfre, ktora nie odpowiada zadnemu trybowi
			printf("Wybrano nieprawidlowy tryb pracy\n");
			wybraneZrodlo = 0;
			break;
	}
	
	return wybraneZrodlo;
}

FILE * otworzPlikOdUzytkownika(void)
{
	char nazwaPliku[100];
	printf("Podaj sciezke do pliku:\n");
	scanf(" %s", nazwaPliku);
	
	return otworzPlikDoOdczytu(nazwaPliku);
}

FILE * otworzURandom(void)
{
	return otworzPlikDoOdczytu("/dev/urandom");
}

FILE * otworzPlikDoOdczytu(char *nazwaPliku)
{
	FILE *wskaznikDoPliku;
	wskaznikDoPliku = fopen(nazwaPliku, "r");
	
	if (wskaznikDoPliku == 0)
	{
		printf("Wystapil blad: \n");
		perror(nazwaPliku);	
	}
	
	return fopen(nazwaPliku, "r");
}

void wyswietlInstrukcje(void)
{
	printf("Program moze pracowac w 3 trybach\n"
			"Tryb 1 - operator wprowadza dane z klawiatury\n"
			"Tryb 2 - dane sa pobierane z podanego pliku\n"
			"Tryb 3 - dane sa pobierane z /dev/urandom\n"
			"\nDo sterowania programem sluza nastepujace sygnaly:\n"
			"SIGINT - sygnal zakonczenia dzialania\n"
			"SIGUSR1 - sygnal wstrzymania dzialania\n"
			"SIGCONT - sygnal wznawienia dzialanie\n"
			"Pidy poszczegolnych procesow zostana wyswietlone po wybraniu trybu pracy\n");	
}

void zamienBajtNaHex(unsigned char bajt, char hexString[])
{
	char symbole[] = "0123456789ABCDEF";
	
	hexString[0] = symbole[bajt / 16];
	hexString[1] = symbole[bajt % 16];
	hexString[2] = '\0';
}

int utworzProcesyPotomne(void)
{
	// funkcja powoluje 3 procesy potomne z procesu macierzystego i zwraca do kazdego procesu jego numer
	// procesy maja nastepujace numery:
	// 0 - pierwszy proces
	// 1 - drugi proces
	// 2 - trzeci proces
	// 3 - proces macierzysty
	
	int numerProcesu;
	pid_t pid;
	
	for (numerProcesu = 0; numerProcesu < 3; numerProcesu++)
	{
		if ((pid = fork()) == 0)
		{
			printf("Proces %d pid: %d\n", numerProcesu + 1, getpid());
			break;
		}
		else
		{
			pidyProcesowPotomnych[numerProcesu] = pid;
		}
	}
	
	return numerProcesu;
}


void wykonajKodPierwszegoProcesu(FILE *zrodloDanych, int pipe[])
{
	// pierwszy proces czyta wybrane zrodlo danych, znak po znaku
	// po odczytaniu znaku przesyla go przez pipe do procesu 2
	// konczy dzialanie 'sam z siebie' tylko w przypadku pracy z plikiem,
	// po przeczytaniu calego pliku
	// w pozostalych trybach dziala w nieskonczonosc i trzeba go zakonczyc wysylajac sygnal
	
	// przygotowanie pipe do wysylania
	close(pipe[0]);
	
	int bajt;
	
	usleep(1000);
	printf("Wcisnij enter, zeby zaczac prace programu.\n");
	getchar();
	getchar();
	
	while(1)
	{
		if (czyProgramWstrzymany == 0)
		{
			bajt = fgetc(zrodloDanych);
			
			// jezeli nie dotarlismy do konca pliku to wysylamy dane dalej
			if (bajt != EOF)
			{
				write(pipe[1], &bajt, sizeof bajt);
			}
			else
			{
				// w przeciwnym wypadku, jezeli nie wystapil zaden blad to wysylamy znak konca pliku dalej
				// i konczymy dzialanie tego procesu
				if (ferror(zrodloDanych) == 0)
				{
					write(pipe[1], &bajt, sizeof bajt);
					close(pipe[1]);
					break;
				}
				else
				{
					// jezeli wystapil blad to prawdopodobnie jest to blad wynikajacy z tego,
					// ze jezeli proces czeka na wprowadzenie danych przez uzytkownika (z klawiatury)
					// i wyslemy do niego sygnal wstrzymania pracy to read konczy sie bledem
					// czyscimy blad i przechodzimy do kolejnej iteracji, zeby wrocic do czekania na dane
					clearerr(zrodloDanych);
				}
			}
		}
	}
}

void wykonajKodDrugiegoProcesu(int pipe[])
{
	// przygotowanie pipe do odbierania i fifo do zapisu
	close(pipe[1]);
	int fifo = open(NAZWA_FIFO, O_WRONLY);
	
	char daneHex[3];
	
	while (1)
	{
		if (czyProgramWstrzymany == 0)
		{
			int odebraneDane;
			
			if(read(pipe[0], &odebraneDane, sizeof odebraneDane) > 0)
			{	
				if (odebraneDane != EOF)
				{
					zamienBajtNaHex(odebraneDane, daneHex);
					write(fifo, daneHex, 3);
				}
				else
				{
					// jezeli 1 proces wyslal znacznik konca pliku (EOF) to 2 proces sygnalizuje to 3
					// poprzez wyslanie tablicy, w ktorej pod indeksem 2 jest wartosc rozna od NULL (np. 1)
					daneHex[2] = 1;
					write(fifo, daneHex, 3);
					break;
				}
			}
		}
	}
	
	close(fifo);
}

void wykonajKodTrzeciegoProcesu(void)
{
	// przygotowanie fifo do odczytu
	int fifo = open(NAZWA_FIFO, O_RDONLY);
	char bufor[3];
	int liczbaOdczytanychBlokowDanych = 0;
	
	while (1)
	{
		if (czyProgramWstrzymany == 0)
		{
			if (read(fifo, bufor, 3) > 0)
			{	
				if (bufor[2] == '\0')
				{
					// jezeli pod 2 indeksem znajduje sie '\0' to znaczy, ze dostalismy normalne dane
					liczbaOdczytanychBlokowDanych++;
					printf("%s ", bufor);
					fflush(stdout);
					if ((liczbaOdczytanychBlokowDanych == 15) || (trybPracy == 1 && bufor[0] == '0' && bufor[1] == 'A'))
					{
						printf("\n");
						liczbaOdczytanychBlokowDanych = 0;
					}
				}
				else
				{
					// jezeli pod indeksem 2 bylo cos innego to znaczy, ze skonczyl nam sie plik
					break;
				}
			}
		}
	}
		
	close(fifo);
}

void wykonajKodProcesuMacierzystego(void)
{
	czekajNaZakonczenieProcesowPotomnych();
	posprzatajPoProgramie();
}

void czekajNaZakonczenieProcesowPotomnych(void)
{
	int i;
	for (i = 0; i < 3; i++)
	{
		wait();
	}
}

void posprzatajPoProgramie(void)
{
	unlink(NAZWA_FIFO);
	shmdt(pidyProcesowPotomnych);
	shmdt(kodOtrzymanegoSygnalu);
}

void * utworzPamiecWspoldzielona(int rozmiar)
{
	int idPamieci = shmget(IPC_PRIVATE, rozmiar, 0600);	
	void *adres;	
	adres = shmat(idPamieci, NULL, 0);
	shmctl(idPamieci, IPC_RMID, NULL);
	
	return adres;
}

void zarejestrujSygnaly(void)
{
	sigset(SIGINT, odbierzSygnal);
	sigset(SIGCONT, odbierzSygnal);
	sigset(SIGUSR1, odbierzSygnal);
	sigset(SIGUSR2, odbierzSIGUSR2);
}

void odbierzSygnal(int kodSygnalu)
{	
	poinformujPozostaleProcesy(kodSygnalu);
	obsluzSygnal(kodSygnalu);
}

void poinformujPozostaleProcesy(int kodSygnalu)
{
	// umieszcza kod otrzymanego sygnalu w pamieci wspoldzieolnej
	// nastepnie wysyla SIGUSR2 do 2 pozostalych procesow
	(*kodOtrzymanegoSygnalu) = kodSygnalu;
	int i;	
	for (i = 0; i < 3; i++)
	{
		if (pidyProcesowPotomnych[i] != getpid())
		{
			kill(pidyProcesowPotomnych[i], SIGUSR2);
		}
	}
}

void odbierzSIGUSR2(int signum)
{
	// funkcja uruchamiana po otrzymaniu sygnalu SIGUSR2
	// przekazuje do dalszej obslugi kod sygnalu zapisany w pamieci wspoldzielonej
	obsluzSygnal((*kodOtrzymanegoSygnalu));
}

void obsluzSygnal(int kodSygnalu)
{
	switch (kodSygnalu)
	{
		case SIGINT:
			exit(0);
			break;
		case SIGUSR1:
			czyProgramWstrzymany = 1;
			break;
		case SIGCONT:
			czyProgramWstrzymany = 0;
			break;
	}
}

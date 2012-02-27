#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <strings.h>
#include <unistd.h>

#define TDO_GPIO 16
#define TMS_GPIO 18
#define TCK_GPIO 20
#define TDI_GPIO 34

#define GPIO_PATH "/sys/class/gpio"
#define EXPORT_PATH GPIO_PATH "/export"
#define UNEXPORT_PATH GPIO_PATH "/unexport"

struct jtag_state {
	int tdi;
	int tms;
	int tck;
	int tdo;
};


static int gpio_export_unexport(char *path, int gpio) {
	int fd = open(path, O_WRONLY);
	char str[16];
	int bytes;

	if (fd == -1) {
		perror("Unable to find GPIO files -- /sys/class/gpio enabled?");
		return -errno;
	}

	bytes = snprintf(str, sizeof(str)-1, "%d", gpio) + 1;

	if (-1 == write(fd, str, bytes)) {
		perror("Unable to modify gpio");
		close(fd);
		return -errno;
	}

	close(fd);
	return 0;
}

static int gpio_export(int gpio) {
	return gpio_export_unexport(EXPORT_PATH, gpio);
}

static int gpio_unexport(int gpio) {
	return gpio_export_unexport(UNEXPORT_PATH, gpio);
}

static int gpio_set_direction(int gpio, int is_output) {
	char gpio_path[256];
	int fd;
	int ret;

	snprintf(gpio_path, sizeof(gpio_path)-1, GPIO_PATH "/gpio%d/direction", gpio);

	fd = open(gpio_path, O_WRONLY);
	if (fd == -1) {
		fprintf(stderr, "Direction file: [%s]\n", gpio_path);
		perror("Couldn't open direction file for gpio");
		return -errno;
	}

	if (is_output)
		ret = write(fd, "out", 4);
	else
		ret = write(fd, "in", 3);

	if (ret == -1) {
		perror("Couldn't set output direction");
		close(fd);
		return -errno;
	}

	close(fd);
	return 0;
}


static int gpio_set_value(int gpio, int value) {
	char gpio_path[256];
	int fd;
	int ret;

	snprintf(gpio_path, sizeof(gpio_path)-1, GPIO_PATH "/gpio%d/value", gpio);

	fd = open(gpio_path, O_WRONLY);
	if (fd == -1) {
		fprintf(stderr, "Value file: [%s]\n", gpio_path);
		perror("Couldn't open value file for gpio");
		return -errno;
	}

	if (value)
		ret = write(fd, "1", 2);
	else
		ret = write(fd, "0", 2);

	if (ret == -1) {
		perror("Couldn't set output value");
		close(fd);
		return -errno;
	}

	close(fd);
	return 0;
}


static int gpio_get_value(int gpio) {
	char gpio_path[256];
	int fd;

	snprintf(gpio_path, sizeof(gpio_path)-1, GPIO_PATH "/gpio%d/value", gpio);

	fd = open(gpio_path, O_RDONLY);
	if (fd == -1) {
		perror("Couldn't open value file for gpio");
		return -errno;
	}

	if (read(fd, gpio_path, sizeof(gpio_path)) <= 0) {
		perror("Couldn't get input value");
		close(fd);
		return -errno;
	}

	close(fd);

	return gpio_path[0] != '0';
}


/* Wiggle the TCK like, moving JTAG one step further along its state machine */
static int jtag_tick(struct jtag_state *state) {
	gpio_set_value(state->tck, 0);
	gpio_set_value(state->tck, 1);
	gpio_set_value(state->tck, 0);
	return 0;
}


/* Send five 1s through JTAG, which will bring it into reset state */
static int jtag_reset(struct jtag_state *state) {
	int i;
	for (i=0; i<5; i++) {
		gpio_set_value(state->tms, 1);
		jtag_tick(state);
	}

	return 0;
}


static int jtag_open(struct jtag_state *state) {

	gpio_export(TDI_GPIO);
	gpio_export(TMS_GPIO);
	gpio_export(TCK_GPIO);
	gpio_export(TDO_GPIO);

	gpio_set_direction(TDI_GPIO, 0);
	gpio_set_direction(TMS_GPIO, 1);
	gpio_set_direction(TCK_GPIO, 1);
	gpio_set_direction(TDO_GPIO, 1);

	gpio_set_value(TDO_GPIO, 0);
	gpio_set_value(TMS_GPIO, 0);
	gpio_set_value(TCK_GPIO, 0);

	state->tdi = TDI_GPIO;
	state->tms = TMS_GPIO;
	state->tck = TCK_GPIO;
	state->tdo = TDO_GPIO;

	jtag_reset(state);

	return 0;
}

/* Reads the ID CODE out of the FPGA
 * When the state machine is reset, the sequence 0, 1, 0, 0 will move
 * it to a point where continually reading the TDO line will yield the
 * ID code.
 *
 * This is because by default, the reset command loads the chip's ID
 * into the data register, so all we have to do is read it out.
 */
static int jtag_idcode(struct jtag_state *state) {
	int i;
	int val = 0;

	/* Reset the state machine */
	jtag_reset(state);

	/* Get into "Run-Test/ Idle" state */
	gpio_set_value(state->tms, 0);
	jtag_tick(state);

	/* Get into "Select DR-Scan" state */
	gpio_set_value(state->tms, 1);
	jtag_tick(state);

	/* Get into "Capture DR" state */
	gpio_set_value(state->tms, 0);
	jtag_tick(state);

	/* Get into "Shift-DR" state */
	gpio_set_value(state->tms, 0);
	jtag_tick(state);

	/* Read the code out */
	for (i=0; i<32; i++) {
		int ret = gpio_get_value(state->tdi);
		val |= (ret<<i);
		jtag_tick(state);
	}

	return val;
}

/* Close GPIOs and return everything to how it was */
static void cleanup(void) {
	gpio_unexport(TDI_GPIO);
	gpio_unexport(TMS_GPIO);
	gpio_unexport(TCK_GPIO);
	gpio_unexport(TDO_GPIO);
}


int main(int argc, char **argv) {
	struct jtag_state state;
	bzero(&state, sizeof(state));

	atexit(cleanup);
	jtag_open(&state);

	printf("ID code: 0x%08x\n", jtag_idcode(&state));

	return 0;
}

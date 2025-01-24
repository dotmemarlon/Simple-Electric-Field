#include <SDL2/SDL.h>

#ifdef __EMSCRIPTEN__
	#include <emscripten.h>
	EM_JS(int, window_get_width, (), {
		return window.screen.width;
	});

	EM_JS(int, window_get_height, (), {
		return window.screen.height;
	});	
#endif

#include <math.h>
#include <time.h>
#include <stdlib.h>
#define PI 3.1415926535f

#define MIN_DELTA_TIME (1.0f / 60)

#define LINE_ANGLE_OFFSET 0.0f
#define LINE_SEGMENT_PIECES 10
#define LINE_PIECE_LENGTH (0.002f / LINE_SEGMENT_PIECES)

#define MAX_LINE_SEGMENTS_NORMAL 100
#define MAX_LINE_SEGMENTS_EXTENDED (100 * 1000)

#define CHARGE_COLORED_RADIUS 0.008f

#define ZOOM 1000.f

#define CHARGE_RADIUS (0.010f * 1000.f / ZOOM)

#define PARTICLE_DEST_SIZE 		 20
#define PARTICLE_TXT_DIAMETER  300

uint32_t W_WIDTH = 1080;
uint32_t W_HEIGHT= 640;

float max_intensity = 0;
float positive_charge_sum = 0;
unsigned int line_count = 0;
unsigned int max_line_segments = MAX_LINE_SEGMENTS_NORMAL;
unsigned int preferred_line_count = 50;

/*
typedef struct {
	float x;
	float y;
	float length;
} Vertex;
*/
typedef SDL_FPoint Vertex;
Vertex** vertices;
int* segment_count;

typedef struct {
	float x;
	float y;
	float q;
} Particle;

float pythago_sqr(float a,float b) {
	return a * a + b * b;
}

void createCircle(uint32_t *data,int r, uint32_t color) {
	for (int i = 0;i < r * 2; ++i) {
		for (int j = 0;j < r * 2; ++j) {
			if (pythago_sqr(i - r, j -r) <= r * r) {
				data[i * r * 2 + j] = color;
			}
			if (r * r - pythago_sqr(i - r, j -r) >= 0 && r * r - pythago_sqr(i-r,j-r) <= 8000) {
				data[i*r*2+j] = 0xff362a28;
			}
		}
	}
}

void drawAxis(SDL_Renderer* renderer, SDL_FPoint axis_point) {
	SDL_RenderDrawLineF(renderer, axis_point.x, axis_point.y, 0, axis_point.y);
	SDL_RenderDrawLineF(renderer, axis_point.x, axis_point.y, W_WIDTH, axis_point.y);
	SDL_RenderDrawLineF(renderer, axis_point.x, axis_point.y, axis_point.x, 0);
	SDL_RenderDrawLineF(renderer, axis_point.x, axis_point.y, axis_point.x, W_HEIGHT);
}

void drawGrid(SDL_Renderer* renderer, SDL_FPoint axis_point) {
	for (float i = axis_point.x; i < W_WIDTH; i+=16) {
		SDL_RenderDrawLineF(renderer, i, 0, i, W_HEIGHT);
	}
	for (float i = axis_point.x; i > 0; i-=16) {
		SDL_RenderDrawLineF(renderer, i, 0, i, W_HEIGHT);
	}
	for (float i = axis_point.y; i < W_HEIGHT; i+=16) {
		SDL_RenderDrawLineF(renderer, 0, i, W_WIDTH, i);
	}
	for (float i = axis_point.y; i > 0; i-=16) {
		SDL_RenderDrawLineF(renderer, 0, i, W_WIDTH, i);
	}
}

SDL_FRect adjustToAxis(SDL_FRect rect, SDL_FPoint axis_point) {
	rect.x *= ZOOM;
	rect.y *= ZOOM;
	rect.x += axis_point.x - rect.w/2;
	rect.y += axis_point.y - rect.h/2;
	return rect;
}

void drawToAxis(SDL_Renderer* renderer, SDL_Texture* texture,SDL_Rect src, SDL_FRect dest, SDL_FPoint axis_point) {
	dest = adjustToAxis(dest, axis_point);
	SDL_RenderCopyF(renderer, texture, &src, &dest);
}

int isPointInside(SDL_FPoint point, SDL_FRect rect) {
	if (point.x >= rect.x && point.x <= rect.x + rect.w && point.y >= rect.y && point.y <= rect.y + rect.h) return 1;
	return 0;
}

float length(SDL_FPoint point) {
	return sqrt(pythago_sqr(point.x,point.y));
}

void normalize(SDL_FPoint * point) {
	float leng = length(*point);
	if (leng != 0) {
		point->x /= leng;
		point->y /= leng;
	}
}

void normalize_with_length(SDL_FPoint * point, float leng) {
	if (leng != 0) {
		point->x /= leng;
		point->y /= leng;
	}
}

void clear() {
	for (unsigned int i = 0; i < line_count; i++) {
		free(vertices[i]);
	}
	free(vertices);
	free(segment_count);

	positive_charge_sum = 0.0f;
	max_line_segments = MAX_LINE_SEGMENTS_NORMAL;
	line_count = 0;
	max_intensity = 0;
}


void initialize(Particle* particles, int particle_num) {
  clear(); 

  float charge_sum = 0.0f;
  unsigned int positive_charges = 0;
  for (unsigned int i = 0; i < particle_num; i++) {
    float charge = particles[i].q; 
    charge_sum += charge;
    if (charge > 0) {
      positive_charge_sum += charge;
      positive_charges++;
    }
  }

  if (charge_sum <= 0) {
    max_line_segments = MAX_LINE_SEGMENTS_EXTENDED;
  }

  for (unsigned int i = 0; i < particle_num; i++) {
    float charge = particles[i].q; 
    if (charge > 0) {
      line_count += (unsigned int)(charge * preferred_line_count / positive_charge_sum); 
    }
  }

	vertices = SDL_calloc (line_count, sizeof(Vertex*) );
	segment_count = calloc(line_count, sizeof(*segment_count) );
}


void add_to_vertex_array(Vertex* vertex_array, unsigned int segment, SDL_FPoint position, float leng) {
		unsigned int index = segment;
		vertex_array[index].x = position.x;
		vertex_array[index].y = position.y;
		//vertex_array[index].length = leng;
}

unsigned int calculate_line(Particle* particles, int particle_num ,unsigned int line, SDL_FPoint position) {
		Vertex* vertex_array = (Vertex*)SDL_malloc(max_line_segments * sizeof(*vertex_array));
		unsigned int segments = 0;
		float leng = 0.0f;
		for (unsigned int piece = 0; piece <= (max_line_segments - 1) * LINE_SEGMENT_PIECES; piece++) {
			SDL_FPoint field_sum = {0.0f, 0.0f};

			for (unsigned int charge_index = 0; charge_index < particle_num; charge_index++) {
				Particle charge = particles[charge_index];
				SDL_FPoint field = {position.x - charge.x, position.y - charge.y};

				float distance = length(field);
				if (distance <= CHARGE_RADIUS && charge.q < 0) {
					if (piece != 0 && piece % LINE_SEGMENT_PIECES != 1) {
						add_to_vertex_array(vertex_array, segments++, position, leng);
					}

					if (segments != 0) {
						vertices[line] = (Vertex*)SDL_realloc(vertex_array, segments * sizeof(*vertex_array));
					}
					else {
						vertices[line] = 0;
						SDL_free(vertex_array);
					}
					return segments;
				}

				if (distance != 0) {
					field.x *= charge.q / (distance * distance);
					field.y *= charge.q / (distance * distance);
				}
				field_sum.x += field.x;
				field_sum.y += field.y;
			}

			leng = length(field_sum);
			if (max_intensity < leng) {
				max_intensity = leng;
			}

			normalize_with_length(&field_sum, leng);
			field_sum.x *= LINE_PIECE_LENGTH;
			field_sum.y *= LINE_PIECE_LENGTH;
			position.x += field_sum.x;
			position.y += field_sum.y;

			if (piece % LINE_SEGMENT_PIECES == 0) {
				add_to_vertex_array(vertex_array, segments++, position, leng);
			}
		}

		vertices[line] = vertex_array;
		return segments;
	}

void calculate_vertices(Particle* particles,int particle_num) {
	int line = 0;
	initialize(particles, particle_num);
	for (int i = 0; i < particle_num; i++) {
		Particle particle = particles[i];
		if (particle.q <= 0) {
			continue;
		}

		unsigned int count = (unsigned int)(particle.q * preferred_line_count / positive_charge_sum);
		for (int i = 1; i <= count; i++) {
			float angle = PI * 2 * i / count;
			SDL_FPoint position = {cos(angle), sin(angle)};
			position.x *= CHARGE_RADIUS -LINE_PIECE_LENGTH ;
			position.y *= CHARGE_RADIUS -LINE_PIECE_LENGTH ;
			position.x += particle.x;
			position.y += particle.y;
			segment_count[line] = calculate_line(particles, particle_num, line, position);
			line++;
		}
	}
	/*

	float log_normalizer = log2(max_intensity);
	for (line = 0; line < line_count; line++) {
		Vertex* array = vertices[line];
		for (unsigned int i = 0; i < segment_count[line]; i++) {
			unsigned int index = i;
			(&array[index])->length = log2((double)(array[index].length)) / log_normalizer;
		}
	}
	*/
}

struct MainContext {
	SDL_Window* window;
	SDL_Renderer* renderer;
	SDL_Event event;


	SDL_Texture* particle_texture_red;
	SDL_Texture* particle_texture_blue;
	Particle* particles;

	int particle_capacity;
	int particle_num;

	SDL_FPoint axis_point;

	int is_hold_down;
	int holded_index;

	uint64_t last_ticks;
	uint64_t current_ticks;

	int _;
};

struct MainContext Init_MainContext() {
	struct MainContext context;
#ifdef __EMSCRIPTEN__
	W_WIDTH = window_get_width();
	W_HEIGHT = window_get_height();
#endif
	context.window = SDL_CreateWindow("Electric line of force", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, W_WIDTH, W_HEIGHT, 0);
	context.renderer = SDL_CreateRenderer(context.window,-1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);


	context.particle_texture_red = SDL_CreateTexture(context.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, PARTICLE_TXT_DIAMETER,PARTICLE_TXT_DIAMETER);
	SDL_SetTextureBlendMode(context.particle_texture_red, SDL_BLENDMODE_BLEND);
	uint32_t* particle_txtdata = SDL_calloc(PARTICLE_TXT_DIAMETER * PARTICLE_TXT_DIAMETER, sizeof(*particle_txtdata));
	createCircle(particle_txtdata, PARTICLE_TXT_DIAMETER / 2, 0xff5555ff);
	SDL_UpdateTexture(context.particle_texture_red, 0, (void*)particle_txtdata, sizeof(*particle_txtdata) * PARTICLE_TXT_DIAMETER);
	SDL_SetTextureScaleMode(context.particle_texture_red, SDL_ScaleModeLinear);

	context.particle_texture_blue = SDL_CreateTexture(context.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, PARTICLE_TXT_DIAMETER,PARTICLE_TXT_DIAMETER);
	SDL_SetTextureBlendMode(context.particle_texture_blue, SDL_BLENDMODE_BLEND);
	createCircle(particle_txtdata, PARTICLE_TXT_DIAMETER / 2, 0xfffde98b);
	SDL_UpdateTexture(context.particle_texture_blue, 0, (void*)particle_txtdata, sizeof(*particle_txtdata) * PARTICLE_TXT_DIAMETER);
	SDL_SetTextureScaleMode(context.particle_texture_blue, SDL_ScaleModeLinear);

	SDL_free(particle_txtdata);

	context.particles = SDL_calloc(5, sizeof(*context.particles));
	context.particles[0].x = 0;
	context.particles[0].y = 0;
	context.particles[0].q = 1;

	context.particle_capacity = 5;
	context.particle_num = 1;

	context.axis_point.x = (float)W_WIDTH/2;
	context.axis_point.y = (float)W_HEIGHT/2;

	context.is_hold_down = 0;
	context.holded_index = 0;

	context.last_ticks = 0;
	context.current_ticks = 0;
	calculate_vertices(context.particles, context.particle_num);
	return context;
}

void Looping(void* contextv) 
{
	struct MainContext* context = contextv;
	context->current_ticks = SDL_GetTicks64();
	while (SDL_PollEvent(&context->event)) {
		if (context->event.type == SDL_QUIT) {
			context->_ = 0;
			return;
		}
		if (context->event.type == SDL_KEYDOWN) {
			if (context->event.key.keysym.sym == SDLK_p) {
				if (context->particle_num >= context->particle_capacity) {
					context->particle_capacity *= 2;
					context->particles = SDL_realloc(context->particles, context->particle_capacity * sizeof(*context->particles));
				}
				context->particles[context->particle_num].x = (float)(rand() % (W_WIDTH  - 2 * PARTICLE_DEST_SIZE) + PARTICLE_DEST_SIZE - context->axis_point.x) / ZOOM;
				context->particles[context->particle_num].y = (float)(rand() % (W_HEIGHT - 2 * PARTICLE_DEST_SIZE) + PARTICLE_DEST_SIZE - context->axis_point.y) / ZOOM;
				context->particles[context->particle_num].q = 1;
				context->particle_num++;
				calculate_vertices(context->particles, context->particle_num);
			}
			if (context->event.key.keysym.sym == SDLK_m) {
				if (context->particle_num >= context->particle_capacity) {
					context->particle_capacity *= 2;
					context->particles = SDL_realloc(context->particles, context->particle_capacity * sizeof(*context->particles));
				}
				context->particles[context->particle_num].x = (float)(rand() % (W_WIDTH  - 2 * PARTICLE_DEST_SIZE) + PARTICLE_DEST_SIZE - context->axis_point.x) / ZOOM;
				context->particles[context->particle_num].y = (float)(rand() % (W_HEIGHT - 2 * PARTICLE_DEST_SIZE) + PARTICLE_DEST_SIZE - context->axis_point.y) / ZOOM;
				context->particles[context->particle_num].q = -1;
				context->particle_num++;
				calculate_vertices(context->particles, context->particle_num);
			}
		}
		if (context->event.type == SDL_MOUSEBUTTONDOWN) {
			if (context->is_hold_down == 0) {
				SDL_FPoint point = {(float)context->event.button.x, (float)context->event.button.y};
				for (int i = 0;i < context->particle_num; ++i) {
					SDL_FRect particle_rect = {context->particles[i].x, context->particles[i].y, (float)PARTICLE_DEST_SIZE, (float)PARTICLE_DEST_SIZE};
					particle_rect = adjustToAxis(particle_rect, context->axis_point);
					if (isPointInside(point, particle_rect)) {
						context->is_hold_down=1;
						context->holded_index = i;
						break;
					}
				}
			} 
		}
		else if (context->event.type == SDL_MOUSEMOTION) {
			if (context->is_hold_down) {
				if (context->current_ticks - context->last_ticks >= 20) {
				context->particles[context->holded_index].x = ((float)context->event.button.x - context->axis_point.x ) / ZOOM;
				context->particles[context->holded_index].y = ((float)context->event.button.y - context->axis_point.y ) / ZOOM;
					context->last_ticks = context->current_ticks;
					calculate_vertices(context->particles, context->particle_num);
				}
			}
		}
		else if (context->event.type == SDL_MOUSEBUTTONUP) {
			context->is_hold_down = 0;
		}
	}
	SDL_SetRenderDrawColor(context->renderer, 40, 42, 54, 0xff);
	SDL_RenderClear(context->renderer);
	SDL_SetRenderDrawColor(context->renderer, 68,71,90,0xff);
	drawGrid(context->renderer, context->axis_point);
	SDL_SetRenderDrawColor(context->renderer, 248,248,242,0xff);
	drawAxis(context->renderer, context->axis_point);

	for (int i = 0;i < line_count; ++i) {
		for (int j = 0;j < segment_count[i]; ++j) {
			SDL_RenderDrawPointF(context->renderer, vertices[i][j].x * ZOOM + context->axis_point.x, vertices[i][j].y * ZOOM + context->axis_point.y);
		}
	}

	for (int i = 0;i < context->particle_num; ++i) {
		SDL_FRect particle_rect = {context->particles[i].x, context->particles[i].y, (float)PARTICLE_DEST_SIZE, (float)PARTICLE_DEST_SIZE};
		particle_rect = adjustToAxis(particle_rect, context->axis_point);
		if (context->particles[i].q < 0) SDL_RenderCopyF(context->renderer, context->particle_texture_blue, 0, &particle_rect);
		else SDL_RenderCopyF(context->renderer, context->particle_texture_red, 0, &particle_rect);
	}
	SDL_RenderPresent(context->renderer);
}

int main() {
	srand(time(0));

	struct MainContext context = Init_MainContext();



#ifdef __EMSCRIPTEN__
	emscripten_set_main_loop_arg(Looping, &context, -1, 1);
#else
	context._ = 1;
	for (;context._;) {
		Looping(&context);
	}
#endif

	clear();
	SDL_free(context.particles);
	SDL_DestroyTexture(context.particle_texture_red);
	SDL_DestroyTexture(context.particle_texture_blue);
	SDL_DestroyRenderer(context.renderer);
	SDL_DestroyWindow(context.window);
	return 0;
}


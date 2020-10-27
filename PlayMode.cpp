#include "PlayMode.hpp"

#include "DrawLines.hpp"
#include "Sound.hpp"
#include "gl_errors.hpp"
#include "data_path.hpp"
#include "hex_dump.hpp"
#include "ColorTextureProgram.hpp"

#include <glm/gtc/type_ptr.hpp>

#include <random>

// CREDITS TO GAME0 BASE CODE!!!!!!!!!!!!!!
// BIG THANKS TO MY VERY FIRST ASSIGNMENT IN THIS COURSE!!!!!!

GLuint vertex_buffer_for_color_texture_program = 0;
GLuint vertex_buffer = 0;
GLuint white_tex = 0;

union D {
	uint32_t i;
	float f;
};

float char_to_float(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
	uint32_t al = (uint32_t) a;
	uint32_t bl = (uint32_t) b;
	uint32_t cl = (uint32_t) c;
	uint32_t dl = (uint32_t) d;

	uint32_t tmp = al | (bl << 8) | (cl << 16) | (dl << 24);
	D dc;
	dc.i = tmp;
	return dc.f;
}

Load< Sound::Sample > wrong_ball(LoadTagDefault, []() -> Sound::Sample const * {
	return new Sound::Sample(data_path("wrong_ball.wav"));
});

PlayMode::PlayMode(Client &client_) : client(client_) {
	position = glm::vec2(1.0f, 1.0f);
	player_positions.resize(4, glm::vec2(0.0f));
	
	//----- allocate OpenGL resources -----
	{ 
		//vertex buffer:
		glGenBuffers(1, &vertex_buffer);
		//for now, buffer will be un-filled.

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	{
		//vertex array mapping buffer for color_texture_program:
		//ask OpenGL to fill vertex_buffer_for_color_texture_program with the name of an unused vertex array object:
		glGenVertexArrays(1, &vertex_buffer_for_color_texture_program);

		//set vertex_buffer_for_color_texture_program as the current vertex array object:
		glBindVertexArray(vertex_buffer_for_color_texture_program);

		//set vertex_buffer as the source of glVertexAttribPointer() commands:
		glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer);

		//set up the vertex array object to describe arrays of PongMode::Vertex:
		glVertexAttribPointer(
			color_texture_program->Position_vec4, //attribute
			3, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 0 //offset
		);
		glEnableVertexAttribArray(color_texture_program->Position_vec4);
		//[Note that it is okay to bind a vec3 input to a vec4 attribute -- the w component will be filled with 1.0 automatically]

		glVertexAttribPointer(
			color_texture_program->Color_vec4, //attribute
			4, //size
			GL_UNSIGNED_BYTE, //type
			GL_TRUE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 //offset
		);
		glEnableVertexAttribArray(color_texture_program->Color_vec4);

		glVertexAttribPointer(
			color_texture_program->TexCoord_vec2, //attribute
			2, //size
			GL_FLOAT, //type
			GL_FALSE, //normalized
			sizeof(Vertex), //stride
			(GLbyte *)0 + 4*3 + 4*1 //offset
		);
		glEnableVertexAttribArray(color_texture_program->TexCoord_vec2);

		//done referring to vertex_buffer, so unbind it:
		glBindBuffer(GL_ARRAY_BUFFER, 0);

		//done setting up vertex array object, so unbind it:
		glBindVertexArray(0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}

	{ 
		//solid white texture:
		//ask OpenGL to fill white_tex with the name of an unused texture object:
		glGenTextures(1, &white_tex);

		//bind that texture object as a GL_TEXTURE_2D-type texture:
		glBindTexture(GL_TEXTURE_2D, white_tex);

		//upload a 1x1 image of solid white to the texture:
		glm::uvec2 size = glm::uvec2(1,1);
		std::vector< glm::u8vec4 > data(size.x*size.y, glm::u8vec4(0xff, 0xff, 0xff, 0xff));
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, size.x, size.y, 0, GL_RGBA, GL_UNSIGNED_BYTE, data.data());

		//set filtering and wrapping parameters:
		//(it's a bit silly to mipmap a 1x1 texture, but I'm doing it because you may want to use this code to load different sizes of texture)
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

		//since texture uses a mipmap and we haven't uploaded one, instruct opengl to make one for us:
		glGenerateMipmap(GL_TEXTURE_2D);

		//Okay, texture uploaded, can unbind it:
		glBindTexture(GL_TEXTURE_2D, 0);

		GL_ERRORS(); //PARANOIA: print out any OpenGL errors that may have happened
	}
}

PlayMode::~PlayMode() {
	//----- free OpenGL resources -----
	glDeleteBuffers(1, &vertex_buffer);
	vertex_buffer = 0;

	glDeleteVertexArrays(1, &vertex_buffer_for_color_texture_program);
	vertex_buffer_for_color_texture_program = 0;

	glDeleteTextures(1, &white_tex);
	white_tex = 0;
}

bool PlayMode::handle_event(SDL_Event const &evt, glm::uvec2 const &window_size) {
	if (evt.type == SDL_MOUSEMOTION) {
		//convert mouse from window pixels (top-left origin, +y is down) to clip space ([-1,1]x[-1,1], +y is up):
		glm::vec2 clip_mouse = glm::vec2(
			(evt.motion.x + 0.5f) / window_size.x * 2.0f - 1.0f,
			(evt.motion.y + 0.5f) / window_size.y *-2.0f + 1.0f
		);
		if (player_pos == 0 || player_pos == 2) {
			float potential_x = (clip_to_court * glm::vec3(clip_mouse, 1.0f)).x;
			potential_x = std::max(potential_x, -court_radius.x + h_paddle_radius.x);
			potential_x = std::min(potential_x, court_radius.x - h_paddle_radius.x);
			position.x = potential_x;
		} else if (player_pos == 1 || player_pos == 3) {
			float potential_y = (clip_to_court * glm::vec3(clip_mouse, 1.0f)).y;
			potential_y = std::max(potential_y, -court_radius.y + v_paddle_radius.y);
			potential_y = std::min(potential_y, court_radius.y - v_paddle_radius.y);
			position.y = potential_y;
		}
		return true;
	}

	return false;
}

void PlayMode::update(float elapsed) {
	// queue data for sending to server:
	if (player_pos >= 0) {
		float x = position.x;
		float y = position.y;
		D dx, dy;
		dx.f = x;
		dy.f = y;
		uint32_t mask = 0xff;
		uint32_t xx = dx.i;
		uint32_t yy = dy.i;
		uint8_t x1 = (uint8_t) (xx & mask);
		uint8_t x2 = (uint8_t) (xx >> 8 & mask);
		uint8_t x3 = (uint8_t) (xx >> 16 & mask);
		uint8_t x4 = (uint8_t) (xx >> 24 & mask);
		uint8_t y1 = (uint8_t) (yy & mask);
		uint8_t y2 = (uint8_t) (yy >> 8 & mask);
		uint8_t y3 = (uint8_t) (yy >> 16 & mask);
		uint8_t y4 = (uint8_t) (yy >> 24 & mask);
		client.connections.back().send('p');
		client.connections.back().send(x1);
		client.connections.back().send(x2);
		client.connections.back().send(x3);
		client.connections.back().send(x4);
		client.connections.back().send(y1);
		client.connections.back().send(y2);
		client.connections.back().send(y3);
		client.connections.back().send(y4);
	}

	//send/receive data:
	client.poll([this](Connection *c, Connection::Event event){
		if (event == Connection::OnOpen) {
			std::cout << "[" << c->socket << "] opened" << std::endl;
		} else if (event == Connection::OnClose) {
			std::cout << "[" << c->socket << "] closed (!)" << std::endl;
			throw std::runtime_error("Lost connection to server!");
		} else { assert(event == Connection::OnRecv);
			std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush();
			while (c->recv_buffer.size() >= 41) {
				(void) this;
				// std::cout << "[" << c->socket << "] recv'd data. Current buffer:\n" << hex_dump(c->recv_buffer); std::cout.flush();
				char type = c->recv_buffer[0];
				int k = 1;
				if (type == 'u' || type == 'a') {
					if (type == 'a') {
						player_pos = (int8_t) c->recv_buffer[k];
						switch (player_pos) {
							case 0:
								position.x = 0.0f;
								position.y = -court_radius.y + 0.5f;
								break;
							case 1:
								position.x = court_radius.x - 0.5f;
								position.y = 0.0f;
								break;
							case 2:
								position.x = 0.0f;
								position.y = court_radius.y - 0.5f;
								break;
							case 3:
								position.x = -court_radius.x + 0.5f;
								position.y = 0.0f;
								break;
							default:
								break;
						}
						k++;
					}
					fall_through = (int8_t) c->recv_buffer[k];
					k++;
					score = (int8_t) c->recv_buffer[k];
					k++;
					player_positions[0].x = char_to_float(c->recv_buffer[k], c->recv_buffer[k+1], c->recv_buffer[k+2], c->recv_buffer[k+3]);
					player_positions[0].y = char_to_float(c->recv_buffer[k+4], c->recv_buffer[k+5], c->recv_buffer[k+6], c->recv_buffer[k+7]);
					player_positions[1].x = char_to_float(c->recv_buffer[k+8], c->recv_buffer[k+9], c->recv_buffer[k+10], c->recv_buffer[k+11]);
					player_positions[1].y = char_to_float(c->recv_buffer[k+12], c->recv_buffer[k+13], c->recv_buffer[k+14], c->recv_buffer[k+15]);
					player_positions[2].x = char_to_float(c->recv_buffer[k+16], c->recv_buffer[k+17], c->recv_buffer[k+18], c->recv_buffer[k+19]);
					player_positions[2].y = char_to_float(c->recv_buffer[k+20], c->recv_buffer[k+21], c->recv_buffer[k+22], c->recv_buffer[k+23]);
					player_positions[3].x = char_to_float(c->recv_buffer[k+24], c->recv_buffer[k+25], c->recv_buffer[k+26], c->recv_buffer[k+27]);
					player_positions[3].y = char_to_float(c->recv_buffer[k+28], c->recv_buffer[k+29], c->recv_buffer[k+30], c->recv_buffer[k+31]);
					ball_pos.x = char_to_float(c->recv_buffer[k+32], c->recv_buffer[k+33], c->recv_buffer[k+34], c->recv_buffer[k+35]);
					ball_pos.y = char_to_float(c->recv_buffer[k+36], c->recv_buffer[k+37], c->recv_buffer[k+38], c->recv_buffer[k+39]);
					if (player_pos != -1) {
						assert(player_pos >= 0 && player_pos <= 3);
						player_positions[player_pos].x = position.x;
						player_positions[player_pos].y = position.y;
					}
				} else {
					throw std::runtime_error("Server sent unknown message type '" + std::to_string(type) + "'");
				}
				c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + k + 40);
			}
		}
	}, 0.0);
	if (score < 0 && !playing) {
		Sound::play_3D(*wrong_ball, 1.0f, glm::vec3(0.0f));
		playing = true;
	}
	if (score > 0) {
		playing = false;
	}
}

void PlayMode::draw(glm::uvec2 const &drawable_size) {
	#define HEX_TO_U8VEC4( HX ) (glm::u8vec4( (HX >> 24) & 0xff, (HX >> 16) & 0xff, (HX >> 8) & 0xff, (HX) & 0xff ))
	const glm::u8vec4 bg_color = HEX_TO_U8VEC4(0x171714ff);
	const glm::u8vec4 fg_color = HEX_TO_U8VEC4(0xd1bb54ff);
	const glm::u8vec4 shadow_color = HEX_TO_U8VEC4(0x604d29ff);
	const glm::u8vec4 red = HEX_TO_U8VEC4(0xff0000ff);
	const std::vector< glm::u8vec4 > rainbow_colors = {
		HEX_TO_U8VEC4(0x604d29ff), HEX_TO_U8VEC4(0x624f29fc), HEX_TO_U8VEC4(0x69542df2),
		HEX_TO_U8VEC4(0x6a552df1), HEX_TO_U8VEC4(0x6b562ef0), HEX_TO_U8VEC4(0x6b562ef0),
		HEX_TO_U8VEC4(0x6d572eed), HEX_TO_U8VEC4(0x6f592feb), HEX_TO_U8VEC4(0x725b31e7),
		HEX_TO_U8VEC4(0x745d31e3), HEX_TO_U8VEC4(0x755e32e0), HEX_TO_U8VEC4(0x765f33de),
		HEX_TO_U8VEC4(0x7a6234d8), HEX_TO_U8VEC4(0x826838ca), HEX_TO_U8VEC4(0x977840a4),
		HEX_TO_U8VEC4(0x96773fa5), HEX_TO_U8VEC4(0xa07f4493), HEX_TO_U8VEC4(0xa1814590),
		HEX_TO_U8VEC4(0x9e7e4496), HEX_TO_U8VEC4(0xa6844887), HEX_TO_U8VEC4(0xa9864884),
		HEX_TO_U8VEC4(0xad8a4a7c),
	};
	#undef HEX_TO_U8VEC4

	//other useful drawing constants:
	const float wall_radius = 0.05f;
	const float padding = 0.14f; //padding between outside of walls and edge of window

	//---- compute vertices to draw ----

	//vertices will be accumulated into this list and then uploaded+drawn at the end of this function:
	std::vector< Vertex > vertices;

	//inline helper function for rectangle drawing:
	auto draw_rectangle = [&vertices](glm::vec2 const &center, glm::vec2 const &radius, glm::u8vec4 const &color) {
		//draw rectangle as two CCW-oriented triangles:
		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));

		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y-radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x+radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
		vertices.emplace_back(glm::vec3(center.x-radius.x, center.y+radius.y, 0.0f), color, glm::vec2(0.5f, 0.5f));
	};

	// solid objects:

	// walls:
	glm::u8vec4 r_fg_color = fg_color;
	if (score < 0) {
		r_fg_color = red;
	}
	if (fall_through != 3) {
		draw_rectangle(glm::vec2(-court_radius.x-wall_radius, 0.0f), glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), r_fg_color);
	}
	if (fall_through != 1) {
		draw_rectangle(glm::vec2( court_radius.x+wall_radius, 0.0f), glm::vec2(wall_radius, court_radius.y + 2.0f * wall_radius), r_fg_color);
	}
	if (fall_through != 2) {
		draw_rectangle(glm::vec2( 0.0f,-court_radius.y-wall_radius), glm::vec2(court_radius.x, wall_radius), r_fg_color);
	}
	if (fall_through != 0) {
		draw_rectangle(glm::vec2( 0.0f, court_radius.y+wall_radius), glm::vec2(court_radius.x, wall_radius), r_fg_color);
	}

	// paddles:
	// draw up
	draw_rectangle(player_positions[0], h_paddle_radius, shadow_color);
	// draw right
	draw_rectangle(player_positions[1], v_paddle_radius, shadow_color);
	// draw down
	draw_rectangle(player_positions[2], h_paddle_radius, shadow_color);
	// draw left
	draw_rectangle(player_positions[3], v_paddle_radius, shadow_color);
	
	//ball:
	if (score >= 0) {
		draw_rectangle(ball_pos, ball_radius, fg_color);
	}

	//------ compute court-to-window transform ------

	//compute area that should be visible:
	glm::vec2 scene_min = glm::vec2(
		-court_radius.x - 2.0f * wall_radius - padding,
		-court_radius.y - 2.0f * wall_radius - padding
	);
	glm::vec2 scene_max = glm::vec2(
		court_radius.x + 2.0f * wall_radius + padding,
		court_radius.y + 2.0f * wall_radius + 3.0f * 0.1 + padding
	);

	//compute window aspect ratio:
	float aspect = drawable_size.x / float(drawable_size.y);
	//we'll scale the x coordinate by 1.0 / aspect to make sure things stay square.

	//compute scale factor for court given that...
	float scale = std::min(
		(2.0f * aspect) / (scene_max.x - scene_min.x), //... x must fit in [-aspect,aspect] ...
		(2.0f) / (scene_max.y - scene_min.y) //... y must fit in [-1,1].
	);

	glm::vec2 center = 0.5f * (scene_max + scene_min);

	//build matrix that scales and translates appropriately:
	glm::mat4 court_to_clip = glm::mat4(
		glm::vec4(scale / aspect, 0.0f, 0.0f, 0.0f),
		glm::vec4(0.0f, scale, 0.0f, 0.0f),
		glm::vec4(0.0f, 0.0f, 1.0f, 0.0f),
		glm::vec4(-center.x * (scale / aspect), -center.y * scale, 0.0f, 1.0f)
	);
	//NOTE: glm matrices are specified in *Column-Major* order,
	// so each line above is specifying a *column* of the matrix(!)

	//also build the matrix that takes clip coordinates to court coordinates (used for mouse handling):
	clip_to_court = glm::mat3x2(
		glm::vec2(aspect / scale, 0.0f),
		glm::vec2(0.0f, 1.0f / scale),
		glm::vec2(center.x, center.y)
	);

	//---- actual drawing ----

	//clear the color buffer:
	glClearColor(bg_color.r / 255.0f, bg_color.g / 255.0f, bg_color.b / 255.0f, bg_color.a / 255.0f);
	glClear(GL_COLOR_BUFFER_BIT);

	//use alpha blending:
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//don't use the depth test:
	glDisable(GL_DEPTH_TEST);

	//upload vertices to vertex_buffer:
	glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer); //set vertex_buffer as current
	glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(vertices[0]), vertices.data(), GL_STREAM_DRAW); //upload vertices array
	glBindBuffer(GL_ARRAY_BUFFER, 0);

	//set color_texture_program as current program:
	glUseProgram(color_texture_program->program);

	//upload OBJECT_TO_CLIP to the proper uniform location:
	glUniformMatrix4fv(color_texture_program->OBJECT_TO_CLIP_mat4, 1, GL_FALSE, glm::value_ptr(court_to_clip));

	//use the mapping vertex_buffer_for_color_texture_program to fetch vertex data:
	glBindVertexArray(vertex_buffer_for_color_texture_program);

	//bind the solid white texture to location zero so things will be drawn just with their colors:
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, white_tex);

	//run the OpenGL pipeline:
	glDrawArrays(GL_TRIANGLES, 0, GLsizei(vertices.size()));

	//unbind the solid white texture:
	glBindTexture(GL_TEXTURE_2D, 0);

	//reset vertex array to none:
	glBindVertexArray(0);

	//reset current program to none:
	glUseProgram(0);
	

	GL_ERRORS(); //PARANOIA: print errors just in case we did something wrong.
	
}

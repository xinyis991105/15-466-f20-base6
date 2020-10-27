#include "Mode.hpp"

#include "Connection.hpp"

#include <glm/glm.hpp>

#include <vector>
#include <deque>

struct PlayMode : Mode {
	PlayMode(Client &client);
	virtual ~PlayMode();

	//functions called by main loop:
	virtual bool handle_event(SDL_Event const &, glm::uvec2 const &window_size) override;
	virtual void update(float elapsed) override;
	virtual void draw(glm::uvec2 const &drawable_size) override;

	//----- game state -----

	struct Vertex {
		Vertex(glm::vec3 const &Position_, glm::u8vec4 const &Color_, glm::vec2 const &TexCoord_) :
			Position(Position_), Color(Color_), TexCoord(TexCoord_) { }
		glm::vec3 Position;
		glm::u8vec4 Color;
		glm::vec2 TexCoord;
	};

	glm::vec2 court_radius = glm::vec2(7.0f, 7.0f);
	glm::vec2 v_paddle_radius = glm::vec2(0.2f, 1.0f);
	glm::vec2 h_paddle_radius = glm::vec2(1.0f, 0.2f);
	glm::vec2 ball_radius = glm::vec2(0.2f, 0.2f);
	
	std::vector<glm::vec2> player_positions;
	glm::vec2 position;
	glm::vec2 ball_pos;
	glm::mat3x2 clip_to_court = glm::mat3x2(1.0f);
	int8_t player_pos = -1;
	int8_t fall_through = -1;
	int8_t score = 0;
	bool playing = false;

	//last message from server:
	std::string server_message;

	//connection to server:
	Client &client;

};


#include "Connection.hpp"

#include "hex_dump.hpp"

#include <chrono>
#include <stdexcept>
#include <iostream>
#include <cassert>
#include <random>
#include <unordered_map>
#include <glm/glm.hpp>

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

int main(int argc, char **argv) {
#ifdef _WIN32
	//when compiled on windows, unhandled exceptions don't have their message printed, which can make debugging simple issues difficult.
	try {
#endif

	//------------ argument parsing ------------

	if (argc != 2) {
		std::cerr << "Usage:\n\t./server <port>" << std::endl;
		return 1;
	}

	//------------ initialization ------------

	Server server(argv[1]);


	//------------ main loop ------------
	constexpr float ServerTick = 1.0f / 20.0f;
	static std::mt19937 mt;

	//server state:

	//per-client state:
	struct PlayerInfo {
		PlayerInfo() {
			static uint32_t next_player_id = 1;
			name = "Player" + std::to_string(next_player_id);
			next_player_id += 1;
		}
		std::string name;
		int8_t position;
		bool just_joined = true;
	};

	std::unordered_map< Connection *, PlayerInfo > players;
	std::vector<glm::vec2> active_players(4);
	active_players[0] = glm::vec2(0.0f, -6.5f);
	active_players[1] = glm::vec2(6.5f, 0.0f);
	active_players[2] = glm::vec2(0.0f, 6.5f);
	active_players[3] = glm::vec2(-6.5f, 0.0f);
	int8_t score = 0;
	float count_down = 4.0f;
	float win_count_down = 2.0f;
	int8_t fall_through = -1;
	bool fallen_through = false;
	glm::vec2 ball_pos = glm::vec2(0.0f);
	glm::vec2 ball_dir = glm::vec2(3.0f, -4.0f);
	glm::vec2 ball_radius = glm::vec2(0.2f, 0.2f);
	glm::vec2 v_paddle_radius = glm::vec2(0.2f, 1.0f);
	glm::vec2 h_paddle_radius = glm::vec2(1.0f, 0.2f);
	glm::vec2 court_radius = glm::vec2(7.0f, 7.0f);
	PlayerInfo *up = nullptr;
	PlayerInfo *down = nullptr;
	PlayerInfo *left = nullptr;
	PlayerInfo *right = nullptr;

	auto send_vec2 = [](glm::vec2 const &pos, Connection *c) {
		float x = pos.x;
		float y = pos.y;
		D dx, dy;
		dx.f = x;
		dy.f = y;
		uint32_t xx = dx.i;
		uint32_t yy = dy.i;
		uint8_t x1 = (uint8_t) (xx & 0xff);
		uint8_t x2 = (uint8_t) (xx >> 8 & 0xff);
		uint8_t x3 = (uint8_t) (xx >> 16 & 0xff);
		uint8_t x4 = (uint8_t) (xx >> 24 & 0xff);
		uint8_t y1 = (uint8_t) (yy & 0xff);
		uint8_t y2 = (uint8_t) (yy >> 8 & 0xff);
		uint8_t y3 = (uint8_t) (yy >> 16 & 0xff);
		uint8_t y4 = (uint8_t) (yy >> 24 & 0xff);
		c->send(x1);
		c->send(x2);
		c->send(x3);
		c->send(x4);
		c->send(y1);
		c->send(y2);
		c->send(y3);
		c->send(y4);
	};

	auto paddle_vs_ball = [&ball_pos, &ball_dir, &ball_radius](glm::vec2 const &paddle, glm::vec2 const &paddle_radius) {
		//compute area of overlap:
		glm::vec2 min = glm::max(paddle - paddle_radius, ball_pos - ball_radius);
		glm::vec2 max = glm::min(paddle + paddle_radius, ball_pos + ball_radius);

		//if no overlap, no collision:
		if (min.x > max.x || min.y > max.y) return false;

		if (max.x - min.x > max.y - min.y) {
			//wider overlap in x => bounce in y direction:
			if (ball_pos.y > paddle.y) {
				ball_pos.y = paddle.y + paddle_radius.y + ball_radius.y;
				ball_dir.y = std::abs(ball_dir.y);
			} else {
				ball_pos.y = paddle.y - paddle_radius.y - ball_radius.y;
				ball_dir.y = -std::abs(ball_dir.y);
			}
		} else {
			//wider overlap in y => bounce in x direction:
			if (ball_pos.x > paddle.x) {
				ball_pos.x = paddle.x + paddle_radius.x + ball_radius.x;
				ball_dir.x = std::abs(ball_dir.x);
			} else {
				ball_pos.x = paddle.x - paddle_radius.x - ball_radius.x;
				ball_dir.x = -std::abs(ball_dir.x);
			}
			//warp x or y velocity based on offset from paddle center:
			if (paddle_radius.y > paddle_radius.x) {
				float vel = (ball_pos.y - paddle.y) / (paddle_radius.y + ball_radius.y);
				ball_dir.y = glm::mix(ball_dir.y, vel, 0.75f);
			} else {
				float vel = (ball_pos.x - paddle.x) / (paddle_radius.x + ball_radius.x);
				ball_dir.x = glm::mix(ball_dir.x, vel, 0.75f);
			}
		}
		return true;
	};

	while (true) {
		static auto next_tick = std::chrono::steady_clock::now() + std::chrono::duration< double >(ServerTick);
		//process incoming data from clients until a tick has elapsed:
		while (true) {
			auto now = std::chrono::steady_clock::now();
			double remain = std::chrono::duration< double >(next_tick - now).count();
			if (remain < 0.0) {
				next_tick += std::chrono::duration< double >(ServerTick);
				break;
			}
			server.poll([&](Connection *c, Connection::Event evt){
				if (evt == Connection::OnOpen) {
					//client connected:
					//create some player info for them:
					auto tmp = PlayerInfo();
					if (up == nullptr) {
						up = &tmp;
						tmp.position = 0;
					} else if (down == nullptr) {
						down = &tmp;
						tmp.position = 2;
					} else if (left == nullptr) {
						left = &tmp;
						tmp.position = 3;
					} else if (right == nullptr) {
						right = &tmp;
						tmp.position = 1;
					} else {
						// watching mode
						tmp.position = -1;
					}
					players.emplace(c, tmp);
				} else if (evt == Connection::OnClose) {
					//client disconnected:
					//remove them from the players list:
					auto f = players.find(c);
					assert(f != players.end());
					int pos = (f->second).position;
					players.erase(f);
					if (pos == 0 || pos == 1 || pos == 2 || pos == 3) {
						if (players.size() > 3) {
							for (auto it = players.begin(); it != players.end(); it++) {
								if (it->second.position == -1) {
									it->second.position = pos;
									auto tmp = it->second;
									switch (pos) {
										case 0:
											up = &tmp;
											break;
										case 1:
											right = &tmp;
											break;
										case 2:
											down = &tmp;
											break;
										default:
											left = &tmp;
											break;
									}
									break;
								}
							}
						} else {
							switch (pos) {
								case 0:
									up = nullptr;
									break;
								case 1:
									right = nullptr;
									break;
								case 2:
									down = nullptr;
									break;
								default:
									left = nullptr;
									break;
							}
						}
					}
				} else { assert(evt == Connection::OnRecv);
					//got data from client:
					std::cout << "got bytes:\n" << hex_dump(c->recv_buffer); std::cout.flush();

					//look up in players list:
					auto f = players.find(c);
					assert(f != players.end());
					PlayerInfo &player = f->second;

					// handle messages from client:
					// expecting to receive the updated location of the player paddle
					while (c->recv_buffer.size() >= 9) {
						// expecting message of this format:
						// - 'p'
						// - 4 bytes of x location (cast to float)
						// - 4 bytes of y location (cast to float)
						char type = c->recv_buffer[0];
						if (type != 'p') {
							std::cout << " message of non-'p' type received from client!" << std::endl;
							//shut down client connection:
							c->close();
							return;
						}
						float x = char_to_float(c->recv_buffer[1], c->recv_buffer[2], c->recv_buffer[3], c->recv_buffer[4]);
						float y = char_to_float(c->recv_buffer[5], c->recv_buffer[6], c->recv_buffer[7], c->recv_buffer[8]);
						assert(player.position >= 0);
						active_players[player.position].x = x;
						active_players[player.position].y = y;
						c->recv_buffer.erase(c->recv_buffer.begin(), c->recv_buffer.begin() + 9);
					}
				}
			}, remain);
		}

		//update current game state
		if (fallen_through) {
			win_count_down -= ServerTick;
			if (win_count_down <= 0.0f) {
				win_count_down = 2.0f;
				fallen_through = false;
				ball_pos.x = 0.0f;
				ball_pos.y = 0.0f;
				fall_through = -1;
				ball_dir.y = std::min(4.0f * ((float) score), 8.0f);
				ball_dir.x = std::min(3.0f * ((float) score), 6.0f);
			}
		} else {
			count_down -= ServerTick;
			if (count_down <= 0.0f) {
				if (fall_through == -1) {
					fall_through = mt() & 0b11;
				} else {
					fall_through = -1;
				}
				count_down = 4.0f;
			}
			paddle_vs_ball(active_players[0], h_paddle_radius);
			paddle_vs_ball(active_players[2], h_paddle_radius);
			paddle_vs_ball(active_players[1], v_paddle_radius);
			paddle_vs_ball(active_players[3], v_paddle_radius);
			
			if (ball_pos.y > court_radius.y - ball_radius.y && fall_through != 0 && !fallen_through) {
				ball_pos.y = court_radius.y - ball_radius.y;
				if (ball_dir.y > 0.0f) {
					ball_dir.y = -ball_dir.y;
				}
			}
			if (ball_pos.y < -court_radius.y + ball_radius.y && fall_through != 2 && !fallen_through) {
				ball_pos.y = -court_radius.y + ball_radius.y;
				if (ball_dir.y < 0.0f) {
					ball_dir.y = -ball_dir.y;
				}
			}

			if (ball_pos.x > court_radius.x - ball_radius.x && fall_through != 1 && !fallen_through) {
				ball_pos.x = court_radius.x - ball_radius.x;
				if (ball_dir.x > 0.0f) {
					ball_dir.x = -ball_dir.x;
				}
			}
			if (ball_pos.x < -court_radius.x + ball_radius.x && fall_through != 3 && !fallen_through) {
				ball_pos.x = -court_radius.x + ball_radius.x;
				if (ball_dir.x < 0.0f) {
					ball_dir.x = -ball_dir.x;
				}
			}

			if (ball_pos.x < -court_radius.x + ball_radius.x ||
				ball_pos.x > court_radius.x - ball_radius.x ||
				ball_pos.y < -court_radius.y + ball_radius.y ||
				ball_pos.y > court_radius.y - ball_radius.y) {
				fallen_through = true;
				score++;
			}

			ball_pos += ServerTick * ball_dir;
		}

		//send updated game state to all clients
		for (auto &[c, player] : players) {
			if (player.just_joined && player.position >= 0) {
				c->send('a');
				c->send(player.position);
				player.just_joined = false;
			} else {
				c->send('u');
			}
			c->send(fall_through);
			if (fallen_through) {
				c->send((int8_t) -score);
			} else {
				c->send(score);
			}
			for (auto it = active_players.begin(); it != active_players.end(); it++) {
				send_vec2((glm::vec2 &) *it, c);
			}
			send_vec2((glm::vec2 &) ball_pos, c);
		}
	}

	return 0;

#ifdef _WIN32
	} catch (std::exception const &e) {
		std::cerr << "Unhandled exception:\n" << e.what() << std::endl;
		return 1;
	} catch (...) {
		std::cerr << "Unhandled exception (unknown type)." << std::endl;
		throw;
	}
#endif
}

#include <stdbool.h>
#include <time.h>
#include <json-c/json_object.h>
#include <json-c/json.h>
#include <libwebsockets.h>

// TODO
static int callback_interscore(struct lws *wsi, enum lws_callback_reasons reason, void *user, void *in, size_t len) {
	switch (reason) {
		case LWS_CALLBACK_RECEIVE:
			lws_write(wsi, (unsigned char *)in, len, LWS_WRITE_TEXT);
			break;
		default:
			printf("TODO: called callback but idk why\n");
			break;
	}
	return 0;
}

static struct lws_protocols protocols[] = {
	{
		.name = "interscore",
		.callback = callback_interscore,
		.per_session_data_size = 0,
		.rx_buffer_size = 0,
		.id = 0,
		.user = NULL,
	},
	{ NULL, NULL, 0, 0 } // Terminator
};

typedef unsigned int uint;

typedef struct {
	uint t1;
	uint t2;
} Score;

typedef struct {
	uint player_index;
	bool card_type; // 0: yellow card, 1: red card
} Card;

typedef struct {
	char *name;
	uint team_index;
	bool role; // 0: keeper, 1: field
} Player;

typedef struct {
	uint keeper_index;
	uint field_index;
	char *name;
	char *logo_filename;
} Team;

typedef struct {
	uint t1_index;
	uint t2_index;
	Score halftimescore;
	Score score;
	Card *cards;
	uint cards_count;
} Game;

typedef struct {
	struct {
		uint gameindex; // index of the current game played in the games array
		bool halftime; // 0: first half, 1: second half
		uint time;
	} cur;
	Game *games;
	uint games_count;
	Team *teams;
	uint teams_count;
	Player *players;
	uint players_count;
} Matchday;

/*
Possible User Actions:
####### Ingame Stuff
- Set Time (DONE)
- Add Time (DONE)
- go a game forward (DONE)
- go a game back (DONE)
- goal Team 1 (DONE)
- goal Team 2 (DONE)
- Yellow Card to Player 1, 2, 3 or 4 (DONE)
- Red Card to Player 1, 2, 3 or 4 (DONE)
- Switch Team Sides (without halftime)
- Half Time
## Error Handling
- minus goal team 1 (DONE)
- minus goal team 2 (DONE)
- delete card (DONE)
####### UI Stuff
- Enable/Disable ==> Ingame Widget
- Start ==> Start of the game/halftime animation
- Enable/Disable ==> Halftime Widget
- enable/Disable ==> Live Table Widget
- Enable/Disable ==> Tunierverlauf Widget
####### Debug Stuff
- Exit (DONE)
- Write State to JSON?
- Reload JSON
- Print State to Terminal?
- Print Connection State to Terminal?
- Print all possible commands
- Print current Gamestate
*/

/* TODO MrMine
- write input.json
- calculate table
*/

// Default length of every halftime in sec
#define GAME_LENGTH 420

// Define the input characters:
// Changing game time
#define ADD_SECOND '+'
#define REMOVE_SECOND '-'
#define SET_TIME 't'

// Switching games
#define GAME_FORWARD 'n'
#define GAME_BACK 'p'

// Goals
#define GOAL_TEAM_1 '1'
#define GOAL_TEAM_2 '2'
#define REMOVE_GOAL_TEAM_1 '3'
#define REMOVE_GOAL_TEAM_2 '4'

// Justice system
#define YELLOW_CARD 'y'
#define RED_CARD 'r'
#define DELETE_CARD 'd'

// Widget toggling
#define TOGGLE_WIDGET_HALFTIME 'h'
#define TOGGLE_WIDGET_INGAME 'i'
#define TOGGLE_WIDGET_LIVETABLE 'l'
#define TOGGLE_WIDGET_RESULTS 'v'

// Meta
#define EXIT 'q'
#define RELOAD_JSON 'j'
#define PRINT_HELP '?'

#define JSON_PATH "input.json"

Matchday md;

// Return the index of a players name.
// If the name does not exist, return -1.
int player_index(const char *name) {
	for (uint i = 0; i < md.players_count; i++)
		if (!strcmp(md.players[i].name, name))
			return i;
	return -1;
}

// Return the index of a team name.
// If the name does not exist return -1.
int team_index(const char *name) {
	for (uint i = 0; i < md.teams_count; i++)
		if (strcmp(md.teams[i].name, name) == 0)
			return i;
	return -1;
}

void load_json(const char *path) {
	//First convert path to actual string containing whole file
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		printf("Json Input file is not available! Exiting...\n");
		exit(EXIT_FAILURE);
	}
	//seek to end to find length, then reset to the beginning
	fseek(f, 0, SEEK_END);
	long file_size = ftell(f);
	rewind(f);

	char *filestring = malloc((file_size + 1) * sizeof(char));
	if (filestring == NULL) {
		printf("Not enough memory for loading json! Exiting...\n");
		fclose(f);
		exit(EXIT_FAILURE);
	}

	long chars_read = fread(filestring, sizeof(char), file_size, f);
	if (chars_read != file_size) {
		printf("Could not read whole json file! Exiting...");
		free(filestring);
		fclose(f);
		exit(EXIT_FAILURE);
	}
	filestring[file_size] = '\0';
	fclose(f);

	// Then split json into teams and games
	struct json_object *root = json_tokener_parse(filestring);
	free(filestring);
	struct json_object *teams = json_object_new_object();
	struct json_object *games = json_object_new_object();
	json_object_object_get_ex(root, "teams", &teams);
	json_object_object_get_ex(root, "games", &games);

	md.teams_count = json_object_object_length(teams);
	md.teams = malloc(md.teams_count * sizeof(Team));

	md.players_count = md.teams_count*2;
	md.players = malloc(md.players_count * sizeof(Player));

	// Read all the teams
	uint i = 0;
	json_object_object_foreach(teams, teamname, teamdata) {
		md.teams[i].name = teamname;
		json_object *logo, *keeper, *field, *name;

		json_object_object_get_ex(teamdata, "logo", &logo);
		md.teams[i].logo_filename = malloc(strlen(json_object_get_string(logo)) * sizeof(char));
		strcpy(md.teams[i].logo_filename, json_object_get_string(logo));

		json_object_object_get_ex(teamdata, "keeper", &keeper);
		json_object_object_get_ex(keeper, "name", &name);
		md.players[i*2].name = malloc(strlen(json_object_get_string(name)) * sizeof(char));
		strcpy(md.players[i*2].name, json_object_get_string(name));
		md.players[i*2].team_index = i;
		md.players[i*2].role = 0;
		md.teams[i].keeper_index = i*2;


		json_object_object_get_ex(teamdata, "field", &field);
		json_object_object_get_ex(field, "name", &name);
		md.players[i*2+1].name = malloc(strlen(json_object_get_string(name)) * sizeof(char));
		strcpy(md.players[i*2+1].name, json_object_get_string(name));
		md.players[i*2+1].team_index = i;
		md.players[i*2+1].role = 1;
		md.teams[i].field_index = i*2+1;

		i++;
	}

	md.games_count = json_object_object_length(games);
	md.games = malloc(md.games_count * sizeof(Game));

	i = 0;
	json_object_object_foreach(games, gamenumber, gamedata) {
		json_object *team;
		json_object_object_get_ex(gamedata, "team1", &team);
		if (team_index(json_object_get_string(team)) == -1) {
			printf("Erorr parsing JSON: '%s' does not exist (teamname of Team 1 in Game %d). Exiting...\n", json_object_get_string(team), i + 1);
			exit(EXIT_FAILURE);
		}
		md.games[i].t1_index = team_index(json_object_get_string(team));

		json_object_object_get_ex(gamedata, "team2", &team);
		if (team_index(json_object_get_string(team)) == -1) {
			printf("Erorr parsing JSON: '%s' does not exist (teamname of Team 2 in Game %d). Exiting...\n", json_object_get_string(team), i + 1);
			exit(EXIT_FAILURE);
		}
		md.games[i].t2_index = team_index(json_object_get_string(team));
		json_object *halftimescore, *score, *cards, *var;
		if (json_object_object_get_ex(gamedata, "halftimescore", &halftimescore)) {
			json_object_object_get_ex(halftimescore, "team1", &var);
			md.games[i].halftimescore.t1 = json_object_get_int(var);
			json_object_object_get_ex(halftimescore, "team2", &var);
			md.games[i].halftimescore.t2 = json_object_get_int(var);
		}
		if (json_object_object_get_ex(gamedata, "score", &score)) {
			json_object_object_get_ex(score, "team1", &var);
			md.games[i].score.t1 = json_object_get_int(var);
			json_object_object_get_ex(score, "team2", &var);
			md.games[i].score.t2 = json_object_get_int(var);
		}
		if (json_object_object_get_ex(gamedata, "cards", &cards)) {

			md.games[i].cards_count = json_object_object_length(cards);
			md.games[i].cards = malloc(md.games[i].cards_count * sizeof(Card));

			uint j = 0;
			json_object_object_foreach(cards, cardname, carddata) {
				json_object *player, *type;
				json_object_object_get_ex(carddata, "player", &player);
				if (player_index(json_object_get_string(player)) == -1) {
					printf("Erorr parsing JSON: '%s' does not exist (Playername of Card %d in Game %d). Exiting...\n", json_object_get_string(player), j+1, i + 1);
				}
				md.games[i].cards[j].player_index = player_index(json_object_get_string(player));

				json_object_object_get_ex(carddata, "type", &type);
				md.games[i].cards[j].card_type = json_object_get_boolean(type);
				j++;
			}
		}
		i++;
	}

	return;
}

// Set current_match to first match
// TODO
void init_matchday() {
	if (md.games_count == 0) {
		printf("There are no games, exiting\n");
		exit(EXIT_FAILURE);
	}
	md.cur.gameindex = 0;
	md.cur.halftime = 0;
	md.cur.time = GAME_LENGTH;
	for (uint i = 0; i < md.games_count; i++) {
		md.games[i].halftimescore.t1 = 0;
		md.games[i].halftimescore.t2 = 0;
		md.games[i].score.t1 = 0;
		md.games[i].score.t2 = 0;
		md.games[i].cards_count = 0;
		md.games[i].cards = NULL;
	}
	return;
}


void add_card(bool card_type) {
	uint ind = md.cur.gameindex;
	if (md.games[ind].cards_count == 0)
		md.games[ind].cards = malloc(1 * sizeof(Card));
	else
		md.games[ind].cards = realloc(md.games[ind].cards, (md.games[ind].cards_count+1) * sizeof(Card));
	printf("Select Player:\n1. %s (Keeper %s)\n2. %s (Field %s)\n3. %s (Keeper %s)\n4. %s (Field %s)\n",
			md.players[md.teams[md.games[ind].t1_index].keeper_index].name, md.teams[md.games[ind].t1_index].name,
			md.players[md.teams[md.games[ind].t1_index].field_index].name, md.teams[md.games[ind].t1_index].name,
			md.players[md.teams[md.games[ind].t2_index].keeper_index].name, md.teams[md.games[ind].t2_index].name,
			md.players[md.teams[md.games[ind].t2_index].field_index].name, md.teams[md.games[ind].t2_index].name);
	uint player;
	scanf("%ud\n", &player);
	switch(player) {
	case 1:
		player = md.teams[md.games[ind].t1_index].keeper_index;
		break;
	case 2:
		player = md.teams[md.games[ind].t1_index].field_index;
		break;
	case 3:
		player = md.teams[md.games[ind].t2_index].keeper_index;
		break;
	case 4:
		player = md.teams[md.games[ind].t2_index].field_index;
		break;
	default:
		printf("Illegal input! Doing nothing");
		return;
	}

	md.games[ind].cards[md.games[ind].cards_count].player_index = player;
	md.games[ind].cards[md.games[ind].cards_count++].card_type = card_type;
	return;
}

int main(void) {
	load_json(JSON_PATH);
	init_matchday();

	struct lws_context_creation_info info;
	struct lws_context *ctx;

	memset(&info, 0, sizeof(info));
	info.port = 8080;
	info.protocols = protocols;

	ctx = lws_create_context(&info);
	if (!ctx) {
		printf("WebSocket init failed!\n");
		return 1;
	}

	printf("WebSocket server started on ws://localhost:8080 ...\n");
	bool close = false;
	while (!close) {
		// TODO WIP
		lws_service(ctx, 0);

		char c = getchar();
		switch (c) {

		// #### INGAME STUFF
		case SET_TIME:
			uint min, sec;
			printf("Current time: %d:%2d\nNew time (in MM:SS): ", md.cur.time/60, md.cur.time%60);
			scanf("%d:%d", &min, &sec); // TODO fix this, %ud breaks sec input
			md.cur.time = min*60 + sec;
			printf("New current time: %d:%2d\n", md.cur.time/60, md.cur.time%60);
			break;
		case ADD_SECOND:
			md.cur.time++;
			printf("Added 1s, new time: %d:%d\n", md.cur.time/60, md.cur.time%60);
			break;
		case REMOVE_SECOND:
			md.cur.time--;
			printf("Removed 1s, new time: %d:%d\n", md.cur.time/60, md.cur.time%60);
			break;
		case GAME_FORWARD:
			if (md.cur.gameindex == md.games_count - 1) {
				printf("Already at last game! Doing nothing ...\n");
				break;
			}
			md.cur.gameindex++;
			md.cur.halftime = 0;
			md.cur.time = GAME_LENGTH;
			printf(
				"New Game %d: %s vs. %s\n",
				md.cur.gameindex + 1,
				md.teams[md.games[md.cur.gameindex].t1_index].name,
				md.teams[md.games[md.cur.gameindex].t2_index].name
			);
			break;
		case GAME_BACK:
			if (md.cur.gameindex == 0) {
				printf("Already at first  ame! Doing nothing ...\n");
				break;
			}
			md.cur.gameindex--;
			md.cur.halftime = 0;
			md.cur.time = GAME_LENGTH;
			printf(
				"New Game %d: %s vs. %s\n",
				md.cur.gameindex+1,
				md.teams[md.games[md.cur.gameindex].t1_index].name,
				md.teams[md.games[md.cur.gameindex].t2_index].name
			);
			break;
		case GOAL_TEAM_1:
			md.games[md.cur.gameindex].score.t1++;
			printf(
				"New Score: %d : %d\n",
				md.games[md.cur.gameindex].score.t1,
				md.games[md.cur.gameindex].score.t2
			);
			break;
		case GOAL_TEAM_2:
			md.games[md.cur.gameindex].score.t2++;
			printf(
				"New Score: %d : %d\n",
				md.games[md.cur.gameindex].score.t1,
				md.games[md.cur.gameindex].score.t2
			);
			break;
		case REMOVE_GOAL_TEAM_1:
			if (md.games[md.cur.gameindex].score.t1 > 0)
				--md.games[md.cur.gameindex].score.t1;
			printf(
				"New Score: %d : %d\n",
				md.games[md.cur.gameindex].score.t1,
				md.games[md.cur.gameindex].score.t2
			);
			break;
		case REMOVE_GOAL_TEAM_2:
			if (md.games[md.cur.gameindex].score.t2 > 0)
				--md.games[md.cur.gameindex].score.t2;
			printf(
				"New Score: %d : %d\n",
				md.games[md.cur.gameindex].score.t1,
				md.games[md.cur.gameindex].score.t2
			);
			break;
		case YELLOW_CARD:
			add_card(0);
			break;
		case RED_CARD:
			add_card(1);
			break;
		case DELETE_CARD:
			uint cur_i = md.cur.gameindex;
			for (uint i = 0; i < md.games[cur_i].cards_count; i++) {
				printf("%d. ", i + 1);
				if (md.games[cur_i].cards[i].card_type == 0)
					printf("Y ");
				else
					printf("R ");
				printf("%s , %s ", md.players[md.games[cur_i].cards[i].player_index].name,
										  md.teams[md.players[md.games[cur_i].cards[i].player_index].team_index].name);
				if (md.players[md.games[cur_i].cards[i].player_index].role == 0)
					printf("(Keeper)\n");
				else
					printf("(field)\n");
			}
			printf("Select a card to delete: ");
			uint c = 0;
			scanf("%ud", &c);
			// Overwrite c with the last element
			md.games[cur_i].cards[c-1] = md.games[cur_i].cards[--md.games[cur_i].cards_count];
			printf("Cards remaining:\n");
			for (uint i = 0; i < md.games[cur_i].cards_count; i++) {
				printf("%d. ", i + 1);
				if (md.games[cur_i].cards[i].card_type == 0)
					printf("Y ");
				else
					printf("R ");
				printf("%s , %s ", md.players[md.games[cur_i].cards[i].player_index].name,
										  md.teams[md.players[md.games[cur_i].cards[i].player_index].team_index].name);
				if (md.players[md.games[cur_i].cards[i].player_index].role == 0)
					printf("(Keeper)\n");
				else
					printf("(field)\n");
			}
			break;
		// #### UI STUFF
		case TOGGLE_WIDGET_INGAME:
			printf("TODO: TOGGLE_WIDGET_INGAME\n");
			break;
		case TOGGLE_WIDGET_HALFTIME:
			printf("TODO: TOGGLE_WIDGET_HALFTIME\n");
			break;
		case TOGGLE_WIDGET_LIVETABLE:
			printf("TODO: WIDGET_LIVETABLE\n");
			break;
		case TOGGLE_WIDGET_RESULTS:
			printf("TODO: TOGGLE_WIDGET_TURNIERVERLAUF\n");
			break;
		// #### Debug Stuff
		case EXIT:
			close = true;
			break;
		case RELOAD_JSON:
			printf("TODO: RELOAD_JSON\n");
			break;
		case PRINT_HELP:
			printf("TODO: PRINT_HELP\n");
			break;
		}
	}

	// TODO WIP
	lws_context_destroy(ctx);
	return 0;
}

/* TODO NOTE lws stuff
int main() {
}
*/

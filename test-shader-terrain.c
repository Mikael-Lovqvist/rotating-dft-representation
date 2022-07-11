#include <stdio.h>
#include "assert.h"
#include "lib_png.h"
#include "lib_glfw.h"
#include "vector_math.h"
#include "keyboard.h"
#include "terrain.h"
#include "textures.h"

#include <time.h>

#include <pthread.h>


#define M_TAU (M_PI + M_PI)

#define SAMPLE_RATE		44100


//#define terrain_size  1999
#define terrain_size  2047


#define TURN_SPEED			500
#define MOVEMENT_SPEED		10000


static struct keyboard_controls {

	kbd_key_state_list states;
	kbd_key_binding_list bindings;
	kbd_control_mapping_list mappings;
	kbd_scalar_control_list scalar_controls;

	struct keyboard_states {
		kbd_key_state abort;
		kbd_key_state rotate_scene;
		kbd_key_state run_compute;
		kbd_key_state wire_frame;
		kbd_key_state triangles;


		kbd_key_state go_forward;
		kbd_key_state go_backward;
		kbd_key_state go_up;
		kbd_key_state go_down;
		kbd_key_state strafe_left;
		kbd_key_state strafe_right;

		kbd_key_state turn_left;
		kbd_key_state turn_right;
		kbd_key_state look_down;
		kbd_key_state look_up;

		
	} named_states;

	struct control_scalars {
		kbd_control_scalar fly_speed;
		kbd_control_scalar turn_speed;
	} named_scalars;

	struct keyboard_bindings {
		kbd_key_binding* abort_press;

		kbd_key_binding* forward_accelerate;
		kbd_key_binding* forward_release;

	} named_bindings;


} keyboard_controls = {0};



float* audio_buffer_data = 0;
bool process_audio;

pthread_t audio_thread;

void* audio_processor_thread(void*) {
	process_audio = true;

	FILE* sound_data = popen("pamon --channels=1 --format=float32 --latency=4096", "r");	//4 bytes per sample

	while(process_audio) {

		fread(audio_buffer_data, sizeof(float), 1024, sound_data);
	}

	return NULL;
}

void start_audio_thread() {
	pthread_create(&audio_thread, NULL, audio_processor_thread, NULL);
}


void run_render_loop(GLFWwindow* window) {
	glfwMakeContextCurrent(window);
	lgl_init();



	//bool load_and_compile_shader_from_file(const GLuint shader_type, GLuint* shader, const char* filename) {
	struct shaders {
		GLuint fragment;
		GLuint vertex;

		GLuint dft1_shader;
		GLuint dft2_shader;
		GLuint dft3_shader;
		GLuint hm_compute_shader;


		GLuint render_program;
		GLuint dft1_program;
		GLuint dft2_program;
		GLuint dft3_program;
		GLuint hm_compute_program;

		GLint link_status;	//TODO - move out of here?
	} shaders = {0};

	struct matrices {
		GLuint projection_attribute;
		GLuint model_attribute;
		GLfloat projection_matrix[16];
		GLfloat model_matrix[16];

	} matrices;

	struct attributes {
		GLuint vertex_array_position;
		GLuint vertex_array_normal;
		GLuint vertex_array_texture_coord;
		GLuint vertex_array;
		GLuint index_array;

	} attributes;


	struct shader_textures {
		GLuint phase_offset;
		GLuint phase_advance;
		GLuint audio_data;
		GLuint pre_accumulate;
		GLuint post_accumulate;
		GLuint height_map1;
		GLuint height_map2;
	} shader_textures;


	bool had_error = false;

	had_error |= !G_assert(load_and_compile_shader_from_file(GL_VERTEX_SHADER, &shaders.vertex, "tests/dft-stage1-vis.vert"), NULL);
	had_error |= !G_assert(load_and_compile_shader_from_file(GL_FRAGMENT_SHADER, &shaders.fragment, "tests/shader3.frag"), NULL);
	
	had_error |= !G_assert(load_and_compile_shader_from_file(GL_COMPUTE_SHADER, &shaders.dft1_shader, "tests/dft-stage1.compute"), NULL);
	had_error |= !G_assert(load_and_compile_shader_from_file(GL_COMPUTE_SHADER, &shaders.dft2_shader, "tests/dft-stage2-vis2.compute"), NULL);
	had_error |= !G_assert(load_and_compile_shader_from_file(GL_COMPUTE_SHADER, &shaders.dft3_shader, "tests/dft-advance-phases.compute"), NULL);
	had_error |= !G_assert(load_and_compile_shader_from_file(GL_COMPUTE_SHADER, &shaders.hm_compute_shader, "tests/vis-height-map-run.compute"), NULL);




	if (had_error) {
		return;
	}


	//RENDER PROGRAM
	shaders.render_program = glCreateProgram();
	glAttachShader(shaders.render_program, shaders.vertex);
	glAttachShader(shaders.render_program, shaders.fragment);
	GL_assert(glLinkProgram(shaders.render_program), NULL);
	
	glGetProgramiv(shaders.render_program, GL_LINK_STATUS, &shaders.link_status);
	if (!G_assert(shaders.link_status == GL_TRUE, NULL)) {
		check_program_logs(shaders.render_program);
		return;
	}


	//DFT 1
	shaders.dft1_program = glCreateProgram();
	glAttachShader(shaders.dft1_program, shaders.dft1_shader);
	GL_assert(glLinkProgram(shaders.dft1_program), NULL);
	
	glGetProgramiv(shaders.dft1_program, GL_LINK_STATUS, &shaders.link_status);
	if (!G_assert(shaders.link_status == GL_TRUE, NULL)) {
		check_program_logs(shaders.dft1_program);
		return;
	}


	//DFT 2
	shaders.dft2_program = glCreateProgram();
	glAttachShader(shaders.dft2_program, shaders.dft2_shader);
	GL_assert(glLinkProgram(shaders.dft2_program), NULL);
	
	glGetProgramiv(shaders.dft2_program, GL_LINK_STATUS, &shaders.link_status);
	if (!G_assert(shaders.link_status == GL_TRUE, NULL)) {
		check_program_logs(shaders.dft2_program);
		return;
	}

	//DFT 3
	shaders.dft3_program = glCreateProgram();
	glAttachShader(shaders.dft3_program, shaders.dft3_shader);
	GL_assert(glLinkProgram(shaders.dft3_program), NULL);
	
	glGetProgramiv(shaders.dft3_program, GL_LINK_STATUS, &shaders.link_status);
	if (!G_assert(shaders.link_status == GL_TRUE, NULL)) {
		check_program_logs(shaders.dft3_program);
		return;
	}


	//Height map compute
	shaders.hm_compute_program = glCreateProgram();
	glAttachShader(shaders.hm_compute_program, shaders.hm_compute_shader);
	GL_assert(glLinkProgram(shaders.hm_compute_program), NULL);
	
	glGetProgramiv(shaders.hm_compute_program, GL_LINK_STATUS, &shaders.link_status);
	if (!G_assert(shaders.link_status == GL_TRUE, NULL)) {
		check_program_logs(shaders.hm_compute_program);
		return;
	}


	GL_assert(matrices.projection_attribute = glGetUniformLocation(shaders.render_program, "projection_matrix"), NULL);
	GL_assert(matrices.model_attribute = glGetUniformLocation(shaders.render_program, "model_matrix"), NULL);

	GL_assert(attributes.vertex_array_position = glGetAttribLocation(shaders.render_program, "position"), NULL);
	GL_assert(attributes.vertex_array_normal = glGetAttribLocation(shaders.render_program, "normal"), NULL);
	GL_assert(attributes.vertex_array_texture_coord = glGetAttribLocation(shaders.render_program, "texture_coord"), NULL);



	terrain_primitive_info terrain_info;

	void** terrain_primitive_indices;
	GLsizei* terrain_primitive_counts;

	terrain_calculate_info(&terrain_info, terrain_size, terrain_size);

	vec2* vertex_data = 0;

	if (A_assert(vertex_data = calloc(terrain_info.vertices, sizeof(vec2)), NULL)) {
		terrain_fill_vec2_buffer(&terrain_info, vertex_data);
		glCreateBuffers(1, &attributes.vertex_array);
		GL_assert(glBindBuffer(GL_ARRAY_BUFFER, attributes.vertex_array), NULL);
		GL_assert(glBufferData(GL_ARRAY_BUFFER, sizeof(vertex_data[0]) * terrain_info.vertices, vertex_data, GL_STATIC_DRAW), NULL);
	}

	free (vertex_data);



	GLuint* element_data = 0;
	//Todo - use some number in terrain_info for how many elements we need
	if (A_assert(element_data = calloc(terrain_info.vertices_per_strip * terrain_info.height, sizeof(GLuint)), NULL)) {
		terrain_fill_index_buffer(&terrain_info, element_data);
		glCreateBuffers(1, &attributes.index_array);
		GL_assert(glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, attributes.index_array), NULL);
		GL_assert(glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(element_data[0]) * terrain_info.vertices_per_strip * terrain_info.height, element_data, GL_STATIC_DRAW), NULL);
	
		terrain_primitive_indices = calloc(terrain_info.height - 1 , sizeof(void*));
		terrain_primitive_counts = calloc(terrain_info.height - 1, sizeof(GLsizei));


		for (int i=0; i<(terrain_info.height - 1); i++) {
			terrain_primitive_indices[i] =(void*) (intptr_t) (i * terrain_info.width * 8);
			terrain_primitive_counts[i] = terrain_info.width * 2;
		}

	}

	free(element_data);


	
	GL_assert(glVertexAttribPointer(attributes.vertex_array_position, 2, GL_FLOAT, GL_FALSE, 0, (void*) 0), NULL);
	GL_assert(glEnableVertexAttribArray(attributes.vertex_array_position), NULL);

	

	if (!GL_assert(create_float_texture(2048, 2048, 2, &shader_textures.height_map1), NULL)) {
		return;
	}

	if (!GL_assert(create_float_texture(2048, 2048, 2, &shader_textures.height_map2), NULL)) {
		return;
	}


	if (!GL_assert(create_float_1D_texture(1024, 1, &shader_textures.phase_offset), NULL)) {
		return;
	}

	if (!GL_assert(create_float_1D_texture(1024, 1, &shader_textures.phase_advance), NULL)) {
		return;
	}

	if (!GL_assert(create_float_1D_texture(1024, 1, &shader_textures.audio_data), NULL)) {
		return;
	}

	if (!GL_assert(create_float_texture(1024, 1024, 2, &shader_textures.pre_accumulate), NULL)) {
		return;
	}

	if (!GL_assert(create_float_1D_texture(1024, 2, &shader_textures.post_accumulate), NULL)) {
		return;
	}


	//PREFILL FREQUENCY ADVANCES


	float* phase_advance_data = calloc(1024, sizeof(float));

	for (int i=0; i < 1024; i++) {
		//float frequency = 10 + powf((float)i, 2)/100;
		//float frequency = 10 + powf((float)i, 0.5)*300;		
		//float frequency = 10 + powf((float)i, 0.5)*30;	
		float frequency = 10 + (float)i * 10;
		phase_advance_data[i] = frequency * M_TAU / SAMPLE_RATE;
	}

		
	glBindTexture(GL_TEXTURE_1D, shader_textures.phase_advance);
	glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, 1024, 0,  GL_RED,  GL_FLOAT, phase_advance_data);


	free(phase_advance_data);




	render_viewport viewport;
	frame_timing frame_time = {
		.max_delta = 0.1,
		.previous = 0,
	};

	//This should be dervied from the camera object
	vec3 projection_plane = {0.8, 1.2, 15000.0};
	float rot=0;

	glEnable(GL_DEPTH_TEST);
	
	//glEnable(GL_PROGRAM_POINT_SIZE);
	glEnable(GL_POINT_SMOOTH);
	//glPointSize(5);

	GL_assert(, NULL);

	vec3 camera_position = v3(0, -1500, -2000);
	vec3 camera_rotation = v3(0, 0, 0);
	float movement_speed = 0;
	float turn_speed = 0;
	float look_speed = 0;
	float strafe_speed = 0;
	float ascension_speed = 0;

	audio_buffer_data = calloc(1024, sizeof(float));

	start_audio_thread();
	bool direction = false;




	while (!glfwWindowShouldClose(window)) {
		calculate_default_viewport(window, &viewport);
		glViewport(0, 0, viewport.width, viewport.height);

		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		//TODO - we should have our own matrix stack

		//Standard projection - this should not be hardcoded
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity();
		glFrustum(-viewport.aspect_ratio*projection_plane.x, viewport.aspect_ratio*projection_plane.x, -projection_plane.x, projection_plane.x, projection_plane.y, projection_plane.z);		



		// RENDER STACK
		
		
		GL_assert(glUseProgram(shaders.render_program), NULL);


		glGetFloatv(GL_PROJECTION_MATRIX, matrices.projection_matrix);
		//glGetFloatv(GL_MODELVIEW_MATRIX, matrices.model_matrix);
		glUniformMatrix4fv(matrices.projection_attribute, 1, GL_FALSE, matrices.projection_matrix);
		glUniformMatrix4fv(matrices.model_attribute, 1, GL_FALSE, matrices.model_matrix);


		glUniform1i(glGetUniformLocation(shaders.render_program, "tex0"), 0);
		GL_assert(glBindTexture(GL_TEXTURE_2D,  shader_textures.height_map2), NULL);
		//GL_assert(glBindImageTexture(0, shader_textures.height_map, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RG32F), NULL);
		


		update_frame_timing(&frame_time);
		//render_frame(window, &viewport, frame_time.delta);

		

		
		
		if (keyboard_controls.named_states.wire_frame.value) {
			glPolygonMode( GL_FRONT_AND_BACK, GL_LINE);
		} else {
			glPolygonMode( GL_FRONT_AND_BACK, GL_FILL);
		}
		
		

		if (keyboard_controls.named_states.triangles.value) {
			GL_assert(glMultiDrawElements(GL_TRIANGLE_STRIP, terrain_primitive_counts, GL_UNSIGNED_INT, (const void* const*) terrain_primitive_indices, terrain_info.height-1), NULL);
		} else {
			GL_assert(glDrawArrays(GL_POINTS, 0, terrain_info.width * terrain_info.height), NULL);
		}



		
		







		glfwSwapBuffers(window);
		glfwPollEvents();
	
		kbd_run_scalar_controls(&keyboard_controls.scalar_controls, frame_time.delta);
		kbd_run_scalar_control_bindings(&keyboard_controls.mappings, frame_time.delta);
		kbd_reset_all_state_edges(&keyboard_controls.states);

		//printf("fly_speed: %f\t%f\n", keyboard_controls.named_scalars.fly_speed.value.current, keyboard_controls.named_scalars.fly_speed.velocity.current);

		//Apply fixed dampening
		
		//keyboard_controls.named_scalars.fly_speed.value.current *= fclamp(1 - frame_time.delta * 10, 0, 1);



		if (keyboard_controls.named_states.look_up.value) {
			look_speed += -TURN_SPEED * frame_time.delta;			
		}

		if (keyboard_controls.named_states.look_down.value) {
			look_speed += TURN_SPEED * frame_time.delta;
		}

		if (keyboard_controls.named_states.turn_left.value) {
			turn_speed += -TURN_SPEED * frame_time.delta;			
		}

		if (keyboard_controls.named_states.turn_right.value) {
			turn_speed += TURN_SPEED * frame_time.delta;
		}

		if (keyboard_controls.named_states.go_forward.value) {
			movement_speed += MOVEMENT_SPEED * frame_time.delta;
		}

		if (keyboard_controls.named_states.go_backward.value) {
			movement_speed += -MOVEMENT_SPEED * frame_time.delta;
		}

		if (keyboard_controls.named_states.strafe_left.value) {
			strafe_speed += MOVEMENT_SPEED * frame_time.delta;
		}

		if (keyboard_controls.named_states.strafe_right.value) {
			strafe_speed += -MOVEMENT_SPEED * frame_time.delta;
		}


		if (keyboard_controls.named_states.go_up.value) {
			ascension_speed += -MOVEMENT_SPEED * frame_time.delta;
		}

		if (keyboard_controls.named_states.go_down.value) {
			ascension_speed += MOVEMENT_SPEED * frame_time.delta;
		}
		
			



		float amount = turn_speed * frame_time.delta;
		camera_rotation.y = fmod(camera_rotation.y + amount, 360);

		amount = look_speed * frame_time.delta;
		camera_rotation.x = fmod(camera_rotation.x + amount, 360);

		amount = movement_speed * frame_time.delta;
		camera_position.x += amount * matrices.model_matrix[2];
		camera_position.y += amount * matrices.model_matrix[6];
		camera_position.z += amount * matrices.model_matrix[10];

		amount = strafe_speed * frame_time.delta;
		camera_position.x += amount * matrices.model_matrix[0];
		camera_position.y += amount * matrices.model_matrix[4];
		camera_position.z += amount * matrices.model_matrix[8];

		amount = ascension_speed * frame_time.delta;
		camera_position.x += amount * matrices.model_matrix[1];
		camera_position.y += amount * matrices.model_matrix[5];
		camera_position.z += amount * matrices.model_matrix[9];

		


		float dampening = fclamp(1- frame_time.delta * 5, 0, 1);
		movement_speed *= dampening;
		strafe_speed *= dampening;
		turn_speed *= dampening;
		ascension_speed *= dampening;
		look_speed *= dampening;

				

		//Update matrix (we should use our own matrix stack for this but now we just do this) TODO
		glMatrixMode(GL_MODELVIEW);
		glLoadIdentity();

		glRotatef(camera_rotation.x, 1, 0, 0);
		glRotatef(camera_rotation.y, 0, 1, 0);
		glTranslatef(camera_position.x, camera_position.y, camera_position.z);

		if (keyboard_controls.named_states.rotate_scene.value) {
			glRotatef(rot, 0, 1, 0);
			rot = fmod(rot + frame_time.delta *10, 360);
		}

		
		glGetFloatv(GL_MODELVIEW_MATRIX, matrices.model_matrix);



		//Check if it is time to exit
		if (keyboard_controls.named_states.abort.value) {
			break;
		}

		if (keyboard_controls.named_states.run_compute.value) {
		

			// GPU DISPATCH

			bool had_error = false;

			//DFT stage 1
			had_error |= !GL_assert(glUseProgram(shaders.dft1_program), NULL);

			glUniform1i(glGetUniformLocation(shaders.dft1_program, "phase_offset"), 0);
			glUniform1i(glGetUniformLocation(shaders.dft1_program, "phase_advance"), 1);
			glUniform1i(glGetUniformLocation(shaders.dft1_program, "audio_data"), 2);
			glUniform1i(glGetUniformLocation(shaders.dft1_program, "pre_accumulate"), 3);

			had_error |= !GL_assert(glBindImageTexture(0, shader_textures.phase_offset, 	0, GL_FALSE, 0, GL_READ_ONLY, 	GL_R32F), NULL);
			had_error |= !GL_assert(glBindImageTexture(1, shader_textures.phase_advance, 	0, GL_FALSE, 0, GL_READ_ONLY, 	GL_R32F), NULL);
			had_error |= !GL_assert(glBindImageTexture(2, shader_textures.audio_data, 		0, GL_FALSE, 0, GL_READ_ONLY, 	GL_R32F), NULL);
			had_error |= !GL_assert(glBindImageTexture(3, shader_textures.pre_accumulate, 	0, GL_FALSE, 0, GL_WRITE_ONLY, 	GL_RG32F), NULL);
			

			had_error |= !GL_assert(glBindTexture(GL_TEXTURE_1D, shader_textures.audio_data), NULL);
			had_error |= !GL_assert(glTexImage1D(GL_TEXTURE_1D, 0, GL_R32F, 1024, 0,  GL_RED,  GL_FLOAT, audio_buffer_data), NULL);


			had_error |= !GL_assert(glDispatchCompute(1024/16, 1024/16, 1), NULL);
			had_error |= !GL_assert(glMemoryBarrier(GL_ALL_BARRIER_BITS), NULL);

			if (had_error) {
				printf("Compute error detected!\n");
				keyboard_controls.named_states.run_compute.value = false;
				check_program_logs(shaders.dft1_program);
			}



			//DFT stage 2 - actually, we will operate on the HM now
			had_error |= !GL_assert(glUseProgram(shaders.dft2_program), NULL);

			glUniform1i(glGetUniformLocation(shaders.dft2_program, "pre_accumulate"), 0);
			glUniform1i(glGetUniformLocation(shaders.dft2_program, "height_map"), 1);

			had_error |= !GL_assert(glBindImageTexture(0, shader_textures.pre_accumulate, 	0, GL_FALSE, 0, GL_READ_ONLY, 	GL_RG32F), NULL);
			had_error |= !GL_assert(glBindImageTexture(1, shader_textures.height_map1, 	0, GL_FALSE, 0, GL_READ_WRITE, 	GL_RG32F), NULL);

		
			had_error |= !GL_assert(glDispatchCompute(1024/16, 1, 1), NULL);
			had_error |= !GL_assert(glMemoryBarrier(GL_ALL_BARRIER_BITS), NULL);



			had_error |= !GL_assert(glUseProgram(0), NULL);

			if (had_error) {
				printf("Compute error detected!\n");
				keyboard_controls.named_states.run_compute.value = false;
				check_program_logs(shaders.dft2_program);
			}


			//DFT stage 3
			had_error |= !GL_assert(glUseProgram(shaders.dft3_program), NULL);

			glUniform1i(glGetUniformLocation(shaders.dft3_program, "phase_offset"), 0);
			glUniform1i(glGetUniformLocation(shaders.dft3_program, "phase_advance"), 1);
			
			had_error |= !GL_assert(glBindImageTexture(0, shader_textures.phase_offset, 	0, GL_FALSE, 0, GL_READ_WRITE, 	GL_R32F), NULL);
			had_error |= !GL_assert(glBindImageTexture(1, shader_textures.phase_advance, 	0, GL_FALSE, 0, GL_READ_ONLY, 	GL_R32F), NULL);
			

			had_error |= !GL_assert(glDispatchCompute(1024/16, 1, 1), NULL);
			had_error |= !GL_assert(glMemoryBarrier(GL_ALL_BARRIER_BITS), NULL);

			if (had_error) {
				printf("Compute error detected!\n");
				keyboard_controls.named_states.run_compute.value = false;
				check_program_logs(shaders.dft3_program);
			}



			//Height map comptue

			had_error |= !GL_assert(glUseProgram(shaders.hm_compute_program), NULL);

			glUniform1i(glGetUniformLocation(shaders.hm_compute_program, "source_texture"), 0);
			glUniform1i(glGetUniformLocation(shaders.hm_compute_program, "destination_texture"), 1);

			direction = !direction;
			if (direction) {
				had_error |= !GL_assert(glBindImageTexture(0, shader_textures.height_map1, 	0, GL_FALSE, 0, GL_READ_ONLY, 	GL_RG32F), NULL);
				had_error |= !GL_assert(glBindImageTexture(1, shader_textures.height_map2, 	0, GL_FALSE, 0, GL_WRITE_ONLY, 	GL_RG32F), NULL);
			} else {
				had_error |= !GL_assert(glBindImageTexture(0, shader_textures.height_map2, 	0, GL_FALSE, 0, GL_READ_ONLY, 	GL_RG32F), NULL);
				had_error |= !GL_assert(glBindImageTexture(1, shader_textures.height_map1, 	0, GL_FALSE, 0, GL_WRITE_ONLY, 	GL_RG32F), NULL);
			}

		
			had_error |= !GL_assert(glDispatchCompute(2048/16, 2048/16, 1), NULL);
			had_error |= !GL_assert(glMemoryBarrier(GL_ALL_BARRIER_BITS), NULL);

			had_error |= !GL_assert(glUseProgram(0), NULL);

			if (had_error) {
				printf("Compute error detected!\n");
				keyboard_controls.named_states.run_compute.value = false;
				check_program_logs(shaders.hm_compute_program);
			}


			//Height map comptue






		}

		errlog_dump_all(stderr);

	}

	

}



void application_key_callback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	kbd_handle_event(&keyboard_controls.bindings, key, action, mods);
}

void application_char_callback(GLFWwindow* window, unsigned int codepoint) {

	printf("char cb: %i\n", codepoint);
}


int main(int argc, char* argv[]) {
	errlog_init(20);
	lglfw_init();
	lpng_init();


	keyboard_controls.named_bindings.abort_press = kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_ESCAPE, GLFW_PRESS, 0, &keyboard_controls.named_states.abort, KBD_SA_SET);

	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_R, GLFW_PRESS, 0, &keyboard_controls.named_states.rotate_scene, KBD_SA_TOGGLE);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_C, GLFW_PRESS, 0, &keyboard_controls.named_states.run_compute, KBD_SA_TOGGLE);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_F, GLFW_PRESS, 0, &keyboard_controls.named_states.wire_frame, KBD_SA_TOGGLE);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_T, GLFW_PRESS, 0, &keyboard_controls.named_states.triangles, KBD_SA_TOGGLE);


	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_LEFT, GLFW_PRESS, 0, &keyboard_controls.named_states.turn_left, KBD_SA_SET);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_LEFT, GLFW_RELEASE, 0, &keyboard_controls.named_states.turn_left, KBD_SA_CLEAR);

	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_RIGHT, GLFW_PRESS, 0, &keyboard_controls.named_states.turn_right, KBD_SA_SET);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_RIGHT, GLFW_RELEASE, 0, &keyboard_controls.named_states.turn_right, KBD_SA_CLEAR);

	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_UP, GLFW_PRESS, 0, &keyboard_controls.named_states.look_up, KBD_SA_SET);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_UP, GLFW_RELEASE, 0, &keyboard_controls.named_states.look_up, KBD_SA_CLEAR);

	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_DOWN, GLFW_PRESS, 0, &keyboard_controls.named_states.look_down, KBD_SA_SET);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_DOWN, GLFW_RELEASE, 0, &keyboard_controls.named_states.look_down, KBD_SA_CLEAR);

	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_W, GLFW_PRESS, 0, &keyboard_controls.named_states.go_forward, KBD_SA_SET);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_W, GLFW_RELEASE, 0, &keyboard_controls.named_states.go_forward, KBD_SA_CLEAR);

	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_A, GLFW_PRESS, 0, &keyboard_controls.named_states.strafe_left, KBD_SA_SET);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_A, GLFW_RELEASE, 0, &keyboard_controls.named_states.strafe_left, KBD_SA_CLEAR);

	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_D, GLFW_PRESS, 0, &keyboard_controls.named_states.strafe_right, KBD_SA_SET);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_D, GLFW_RELEASE, 0, &keyboard_controls.named_states.strafe_right, KBD_SA_CLEAR);

	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_S, GLFW_PRESS, 0, &keyboard_controls.named_states.go_backward, KBD_SA_SET);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_S, GLFW_RELEASE, 0, &keyboard_controls.named_states.go_backward, KBD_SA_CLEAR);

	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_PAGE_UP, GLFW_PRESS, 0, &keyboard_controls.named_states.go_up, KBD_SA_SET);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_PAGE_UP, GLFW_RELEASE, 0, &keyboard_controls.named_states.go_up, KBD_SA_CLEAR);

	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_PAGE_DOWN, GLFW_PRESS, 0, &keyboard_controls.named_states.go_down, KBD_SA_SET);
	kbd_add_key_binding(&keyboard_controls.bindings, GLFW_KEY_PAGE_DOWN, GLFW_RELEASE, 0, &keyboard_controls.named_states.go_down, KBD_SA_CLEAR);


	//keyboard_controls.named_states.rotate_scene.value = true;	

	GLFWwindow* window = glfwCreateWindow(640, 480, argv[0], NULL, NULL);

	if (window) {

		glfwSetKeyCallback(window, application_key_callback);
		glfwSetCharCallback(window, application_char_callback);
		run_render_loop(window);
		glfwDestroyWindow(window);

	}

	process_audio = false;

	glfwTerminate();
	
	errlog_dump_all(stderr);
	lr_dump_all(stderr);

	return 0;
}
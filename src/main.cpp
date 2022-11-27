#include <gl3w/gl3w.h> // to load OpenGL extensions at runtime
#include <GLFW/glfw3.h> // to set up the OpenGL context and manage window lifecycle and inputs
#include "helpers/ProgramUtilities.h"
#include "helpers/Configuration.h"
#include "helpers/ResourcesManager.h"
#include "helpers/ImGuiStyle.h"

#include "rendering/Renderer.h"

#include <imgui/imgui.h>
#include <imgui/imgui_impl_glfw.h>
#include <imgui/imgui_impl_opengl3.h>

#include <nfd.h>
#include <iostream>
#include <algorithm>

/// Callbacks

void resize_callback(GLFWwindow* window, int width, int height){
	Renderer *renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
	renderer->resize(width, height);
}

void rescale_callback(GLFWwindow* window, float xscale, float yscale){
	Renderer *renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
	// Assume only one of the two for now.
	renderer->rescale(xscale);
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods){
	if(!ImGui::GetIO().WantCaptureKeyboard){
		// Get pointer to the renderer.
		Renderer *renderer = static_cast<Renderer*>(glfwGetWindowUserPointer(window));
		renderer->keyPressed(key, action);
	}
	ImGui_ImplGlfw_KeyCallback(window, key, scancode, action, mods);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset){
	ImGui_ImplGlfw_ScrollCallback(window, xoffset, yoffset);
}

/// Perform system window action.

void performAction(SystemAction action, GLFWwindow * window, glm::ivec4 & frame){
	switch (action.type) {
		case SystemAction::FULLSCREEN: {
			// Are we currently fullscreen?
			const bool fullscreen = glfwGetWindowMonitor(window) != nullptr;
			if(fullscreen) {
				// Restore the window position and size.
				glfwSetWindowMonitor(window, nullptr, frame[0], frame[1], frame[2], frame[3], 0);
				// Check the window position and size (if we are on a screen smaller than the initial size).
				glfwGetWindowPos(window, &frame[0], &frame[1]);
				glfwGetWindowSize(window, &frame[2], &frame[3]);
			} else {
				// Backup the window current frame.
				glfwGetWindowPos(window, &frame[0], &frame[1]);
				glfwGetWindowSize(window, &frame[2], &frame[3]);
				// Move to fullscreen on the primary monitor.
				GLFWmonitor * monitor	= glfwGetPrimaryMonitor();
				const GLFWvidmode * mode = glfwGetVideoMode(monitor);
				glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
			}

			// On some hardware, V-sync options can be lost.
			glfwSwapInterval(1);
			break;
		}
		case SystemAction::RESIZE:
			glfwSetWindowSize(window, action.data[0], action.data[1]);
			// Check the window position and size (if we are on a screen smaller than the target size).
			glfwGetWindowPos(window, &frame[0], &frame[1]);
			glfwGetWindowSize(window, &frame[2], &frame[3]);
			break;
		case SystemAction::FIX_SIZE:
			glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_FALSE);
			// This is for recording, to go as fast as possible on the GPU side.
			glfwSwapInterval(0);
			break;
		case SystemAction::FREE_SIZE:
			glfwSetWindowAttrib(window, GLFW_RESIZABLE, GLFW_TRUE);
			// Restore V-sync after recording end.
			glfwSwapInterval(1);
			break;
		case SystemAction::QUIT:
			glfwSetWindowShouldClose(window, GLFW_TRUE);
		default:
			break;
	}
}

/// The main function

int main( int argc, char** argv) {

	const std::string internalConfigPath = "midiviz_internal.settings";

	// Initialize glfw, which will create and setup an OpenGL context.
	if (!glfwInit()) {
		std::cerr << "[ERROR]: could not start GLFW3" << std::endl;
		return 2;
	}

	// This has to be called after glfwInit for the working dir to be OK on macOS.
	Configuration config(internalConfigPath, std::vector<std::string>(argv, argv+argc));

	if(config.showHelp){
		Configuration::printHelp();
		glfwTerminate();
		return 0;
	}
	if(config.showVersion){
		Configuration::printVersion();
		glfwTerminate();
		return 0;
	}
	
	// On OS X, the correct OpenGL profile and version to use have to be explicitely defined.
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

	// Window visiblity and transparency.
	glfwWindowHint(GLFW_VISIBLE, config.hideWindow ? GLFW_FALSE : GLFW_TRUE);
	glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, config.preventTransparency ? GLFW_FALSE : GLFW_TRUE);

	// Create a window with a given size. Width and height are macros as we will need them again.
	GLFWwindow* window = glfwCreateWindow(config.windowSize[0], config.windowSize[1], "MIDI Visualizer", NULL, NULL);
	if (!window) {
		std::cerr << "[ERROR]: could not open window with GLFW3" << std::endl;
		glfwTerminate();
		return 2;
	}
	// Set window position.
	glfwSetWindowPos(window, config.windowPos[0], config.windowPos[1]);
	// Check if transparency was successfully enabled.
	config.preventTransparency = glfwGetWindowAttrib(window, GLFW_TRANSPARENT_FRAMEBUFFER) == GLFW_FALSE;

	// Bind the OpenGL context and the new window.
	glfwMakeContextCurrent(window);

	if (gl3wInit()) {
		std::cerr << "[ERROR]: Failed to initialize OpenGL" << std::endl;
		return -1;
	}
	if (!gl3wIsSupported(3, 2)) {
		std::cerr << "[ERROR]: OpenGL 3.2 not supported\n" << std::endl;
		return -1;
	}

	// The font should be maintained alive until the atlas is built.
	ImFontConfig font;
	// We need a scope to ensure the renderer is deleted before the OpenGL context is destroyed.
	{

		// Setup resources.
		ResourcesManager::loadResources();
		// Create the renderer (passing options to display them)
		Renderer renderer(config);

		// Setup ImGui for interface.
		ImGui::CreateContext();

		ImGui::configureFont(font);
		ImGui::configureStyle();
		
		ImGui_ImplGlfw_InitForOpenGL(window, false);
		ImGui_ImplOpenGL3_Init("#version 330");

		// Load midi file if specified.
		if(!config.lastMidiPath.empty()){
			renderer.loadFile(config.lastMidiPath);
		}
		// Apply custom state.
		State state;
		if(!config.lastConfigPath.empty()){
			state.load(config.lastConfigPath);
		}

		// Apply any extra display argument on top of the existing config.
		state.load(config.args());
		renderer.setState(state);

		// Define utility pointer for callbacks (can be obtained back from inside the callbacks).
		glfwSetWindowUserPointer(window, &renderer);
		glfwSetFramebufferSizeCallback(window, resize_callback);
		glfwSetKeyCallback(window,key_callback);
		glfwSetScrollCallback(window,scroll_callback);
		glfwSetCharCallback(window, ImGui_ImplGlfw_CharCallback);
		glfwSwapInterval(1);

		// On HiDPI screens, we might have to initially resize the framebuffers size.
		glm::ivec4 frame(0);
		glfwGetWindowPos(window, &frame[0], &frame[1]);
		glfwGetWindowSize(window, &frame[2], &frame[3]);
		int width, height;
		glfwGetFramebufferSize(window, &width, &height);
		const float scale = float(width) / float((std::max)(frame[2], 1));
		renderer.resizeAndRescale(width, height, scale);

		// Scale the GUI based on options. This one has to be done late, after ImGui initialisation.
		renderer.setGUIScale(config.guiScale);

		// Direct export.
		const bool directRecord = !config.exporting.path.empty();
		if(directRecord){
			const bool success = renderer.startDirectRecording(config.exporting, config.windowSize);
			if(!success){
				// Quit.
				performAction(SystemAction::QUIT, window, frame);
			}
		}

		if(config.fullscreen){
			performAction(SystemAction::FULLSCREEN, window, frame);
		}

		// Start the display/interaction loop.
		while (!glfwWindowShouldClose(window)) {
			ImGui_ImplOpenGL3_NewFrame();
			ImGui_ImplGlfw_NewFrame();
			ImGui::NewFrame();

			// Update the content of the window.
			SystemAction action = renderer.draw(DEBUG_SPEED * float(glfwGetTime()));

			// Perform system window action if required.
			performAction(action, window, frame);

			// Interface rendering.
			ImGui::Render();
			ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

			//Display the result fo the current rendering loop.
			glfwSwapBuffers(window);
			// Update events (inputs,...).
			glfwPollEvents();

		}

		ImGui_ImplOpenGL3_Shutdown();
		ImGui_ImplGlfw_Shutdown();
		ImGui::DestroyContext();
		renderer.clean();
	}

	// Remove the window.
	glfwDestroyWindow(window);
	// Clean other resources
	// Close GL context and any other GLFW resources.
	glfwTerminate();
	return 0;
}



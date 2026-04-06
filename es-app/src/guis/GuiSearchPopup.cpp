#include "guis/GuiSearchPopup.h"

#include "components/RatingComponent.h"
#include "guis/GuiGamelistOptions.h"
#include "guis/GuiMenu.h"
#include "renderers/Renderer.h"
#include "utils/StringUtil.h"
#include "views/UIModeController.h"
#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "FileData.h"
#include "Log.h"
#include "SystemData.h"
#include "ThemeData.h"
#include "Window.h"
#include <algorithm>
#include <cstring>
#include <cstdlib>  // rand()

GuiSearchPopup::GuiSearchPopup(Window* window, SystemData* scope)
	: GuiComponent(window),
	  mScope(scope), mThemeSystem(nullptr),
	  mSearchText(window), mCharRow(window),
	  mResultList(window),
	  mImage(window), mThumbnail(window),
	  mDescContainer(window, 1500), mDescription(window),
	  mRating(window),
	  mDeveloper(window), mPublisher(window), mGenre(window), mPlayers(window),
	  mLblRating(window), mLblDeveloper(window), mLblPublisher(window),
	  mLblGenre(window), mLblPlayers(window),
	  mCursorPos(0), mCancelFlag(false), mResultsReady(false),
	  mFocus(FOCUS_CHAR_ROW),
	  mResultListSelectorColor(0x000050FF), mResultListSelectorColorEnd(0x000050FF)
{
	const float sw = (float)Renderer::getScreenWidth();
	const float sh = (float)Renderer::getScreenHeight();
	setSize(sw, sh);

	// ── search text ──────────────────────────────────────────────
	mSearchText.setText("");
	mSearchText.setPosition(0, 0);
	mSearchText.setSize(sw, sh * 0.06f);
	mSearchText.setHorizontalAlignment(ALIGN_CENTER);
	mSearchText.setColor(0xCCCCCCFF);
	mSearchText.setFont(Font::get(FONT_SIZE_MEDIUM));
	mSearchText.setDefaultZIndex(40);
	addChild(&mSearchText);

	// ── char row ─────────────────────────────────────────────────
	mCharRow.setPosition(0, sh * 0.06f);
	mCharRow.setSize(sw, sh * 0.07f);
	mCharRow.setDefaultZIndex(40);
	addChild(&mCharRow);

	mCharRow.setCharSelectedCallback([this](const std::string& ch) {
		mQuery.insert(mCursorPos, ch);
		mCursorPos += ch.size();
		updateSearchDisplay();
		startSearch(mQuery);
	});

	mCharRow.setBackspaceCallback([this]()   { editBackspace(); });
	mCharRow.setCursorLeftCallback([this]()  { editCursorLeft(); });
	mCharRow.setCursorRightCallback([this]() { editCursorRight(); });

	// ── result list (left column) ─────────────────────────────────
	mResultList.setPosition(0, sh * 0.13f);
	mResultList.setSize(sw * 0.50f, sh * 0.80f);
	mResultList.setFont(Font::get(FONT_SIZE_SMALL));
	mResultList.setCursorChangedCallback([this](CursorState /*state*/) { updateInfoPanel(); });
	mResultList.setFavoriteIndicatorCallback([](FileData* file) {
		return file != nullptr && file->getType() == GAME &&
		       file->metadata.get("favorite") == "true";
	});
	mResultList.setDefaultZIndex(20);
	addChild(&mResultList);

	// ── metadata panel (right column) ────────────────────────────
	const float rx = sw * 0.53f;

	mImage.setOrigin(0.5f, 0.0f);
	mImage.setPosition(rx + sw * 0.225f, sh * 0.13f);
	mImage.setMaxSize(sw * 0.44f, sh * 0.50f);
	mImage.setDefaultZIndex(30);
	addChild(&mImage);

	mThumbnail.setOrigin(0.5f, 0.0f);
	mThumbnail.setPosition(2.0f, 2.0f);
	mThumbnail.setMaxSize(sw * 0.44f, sh * 0.50f);
	mThumbnail.setVisible(false);
	mThumbnail.setDefaultZIndex(35);
	addChild(&mThumbnail);

	mDescContainer.setPosition(rx, sh * 0.72f);
	mDescContainer.setSize(sw * 0.44f, sh * 0.20f);
	mDescContainer.setAutoScroll(true);
	mDescContainer.setDefaultZIndex(40);
	addChild(&mDescContainer);

	mDescription.setFont(Font::get(FONT_SIZE_SMALL));
	mDescription.setColor(0xDDDDDDFF);
	mDescription.setSize(sw * 0.44f, 0);
	mDescContainer.addChild(&mDescription);

	// stacked metadata rows below description
	const float mh = sh * 0.04f;
	std::shared_ptr<Font> smallFont = Font::get(FONT_SIZE_SMALL);

	auto placeLbl = [&](TextComponent& lbl, const char* text, float y) {
		lbl.setText(text);
		lbl.setFont(smallFont);
		lbl.setColor(0x999999FF);
		lbl.setPosition(rx, y);
		lbl.setSize(sw * 0.12f, mh);
		lbl.setDefaultZIndex(40);
		addChild(&lbl);
	};
	auto placeVal = [&](GuiComponent& val, float y) {
		val.setPosition(rx + sw * 0.13f, y);
		val.setSize(sw * 0.30f, mh);
		val.setDefaultZIndex(40);
		addChild(&val);
	};

	float my = sh * 0.63f;
	placeLbl(mLblRating,    "Rating: ",    my); placeVal(mRating,    my); my += mh;
	placeLbl(mLblDeveloper, "Developer: ", my); placeVal(mDeveloper, my); my += mh;
	placeLbl(mLblGenre,     "Genre: ",     my); placeVal(mGenre,     my); my += mh;
	placeLbl(mLblPlayers,   "Players: ",   my); placeVal(mPlayers,   my); my += mh;
	placeLbl(mLblPublisher, "Publisher: ", my); placeVal(mPublisher, my);

	mDeveloper.setFont(smallFont);
	mDeveloper.setColor(0xDDDDDDFF);
	mGenre.setFont(smallFont);
	mGenre.setColor(0xDDDDDDFF);
	mPlayers.setFont(smallFont);
	mPlayers.setColor(0xDDDDDDFF);
	mPublisher.setFont(smallFont);
	mPublisher.setColor(0xDDDDDDFF);

	// Apply theme from scope system (or first available system for all-systems search)
	SystemData* themeSource = mScope;
	if (!themeSource && !SystemData::sSystemVector.empty())
		themeSource = SystemData::sSystemVector.front();
	if (themeSource)
	{
		applyTheme(themeSource);
		// Apply result list theme once here — never re-applied so scrolling doesn't flicker
		auto theme = themeSource->getTheme();
		mResultList.applyTheme(theme, "search", "gamelist", ThemeFlags::ALL);
		const ThemeData::ThemeElement* gamelistElem =
			theme->getElement("search", "gamelist", "textlist");
		if (gamelistElem)
		{
			if (gamelistElem->has("selectorColor"))
				mResultListSelectorColor = gamelistElem->get<unsigned int>("selectorColor");
			if (gamelistElem->has("selectorColorEnd"))
				mResultListSelectorColorEnd = gamelistElem->get<unsigned int>("selectorColorEnd");
		}
	}

	buildGameCache();
	addPlaceholder("TYPE TO SEARCH...");
	updateFocusVisuals();
}

GuiSearchPopup::~GuiSearchPopup()
{
	cancelSearch();
	for (auto p : mPlaceholders)
		delete p;
	mPlaceholders.clear();
}

void GuiSearchPopup::applyTheme(SystemData* sys)
{
	mThemeSystem = sys;
	if (!sys)
		return;

	auto theme = sys->getTheme();
	using namespace ThemeFlags;

	// Image / thumbnail
	mImage.applyTheme(theme,     "search", "md_image",     POSITION | SIZE | Z_INDEX | ROTATION | VISIBLE);
	mThumbnail.applyTheme(theme, "search", "md_thumbnail", POSITION | SIZE | Z_INDEX | ROTATION | VISIBLE);

	// Description
	mDescContainer.applyTheme(theme, "search", "md_description", POSITION | SIZE | Z_INDEX | VISIBLE);
	mDescription.setSize(mDescContainer.getSize().x(), 0);
	mDescription.applyTheme(theme, "search", "md_description",
		ALL ^ (POSITION | SIZE | ORIGIN | TEXT | ROTATION));

	// Metadata values
	mRating.applyTheme(   theme, "search", "md_rating",    ALL ^ TEXT);
	mDeveloper.applyTheme(theme, "search", "md_developer", ALL ^ TEXT);
	mPublisher.applyTheme(theme, "search", "md_publisher", ALL ^ TEXT);
	mGenre.applyTheme(    theme, "search", "md_genre",     ALL ^ TEXT);
	mPlayers.applyTheme(  theme, "search", "md_players",   ALL ^ TEXT);

	// Labels
	mLblRating.applyTheme(   theme, "search", "md_lbl_rating",    ALL);
	mLblDeveloper.applyTheme(theme, "search", "md_lbl_developer", ALL);
	mLblPublisher.applyTheme(theme, "search", "md_lbl_publisher", ALL);
	mLblGenre.applyTheme(    theme, "search", "md_lbl_genre",     ALL);
	mLblPlayers.applyTheme(  theme, "search", "md_lbl_players",   ALL);
	// Note: mResultList theme is applied once in the constructor — not here —
	// to avoid flickering the favorite icon on every cursor change.
}

void GuiSearchPopup::updateSearchDisplay()
{
	std::string display = mQuery.substr(0, mCursorPos) + "|" + mQuery.substr(mCursorPos);
	mSearchText.setText(display);
}

void GuiSearchPopup::updateInfoPanel()
{
	if (mResultList.size() == 0)
		return;

	FileData* file = mResultList.getSelected();
	if (!file || file->getType() != GAME)
	{
		mImage.setImage("");
		mThumbnail.setImage("");
		mDescription.setText("");
		mRating.setValue("0");
		mDeveloper.setValue("");
		mPublisher.setValue("");
		mGenre.setValue("");
		mPlayers.setValue("");
		return;
	}

	// Dynamic theme reload when cursor moves to a game from a different system.
	// mResultList is intentionally excluded from applyTheme() to avoid flickering.
	SystemData* sys = file->getSystem();
	if (sys != mThemeSystem)
		applyTheme(sys);

	mImage.setImageAsync(file->getImagePath());
	mThumbnail.setImageAsync(file->getThumbnailPath());
	mDescription.setText(file->metadata.get("desc"));
	mDescription.setSize(mDescription.getSize().x(), 0); // auto-height
	mDescContainer.reset();

	mRating.setValue(file->metadata.get("rating"));
	mDeveloper.setValue(file->metadata.get("developer"));
	mPublisher.setValue(file->metadata.get("publisher"));
	mGenre.setValue(file->metadata.get("genre"));
	mPlayers.setValue(file->metadata.get("players"));
}

void GuiSearchPopup::editBackspace()
{
	if (mCursorPos > 0)
	{
		size_t prev = Utils::String::prevCursor(mQuery, mCursorPos);
		mQuery.erase(prev, mCursorPos - prev);
		mCursorPos = prev;
	}
	updateSearchDisplay();
	startSearch(mQuery);
}

void GuiSearchPopup::editCursorLeft()
{
	if (mCursorPos > 0)
		mCursorPos = Utils::String::prevCursor(mQuery, mCursorPos);
	updateSearchDisplay();
}

void GuiSearchPopup::editCursorRight()
{
	if (mCursorPos < mQuery.size())
		mCursorPos = Utils::String::nextCursor(mQuery, mCursorPos);
	updateSearchDisplay();
}

void GuiSearchPopup::buildGameCache()
{
	mAllGames.clear();
	mLowerNames.clear();

	auto addSystem = [&](SystemData* sys) {
		if (!sys->isGameSystem() || sys->isCollection())
			return;
		for (auto game : sys->getRootFolder()->getFilesRecursive(GAME))
		{
			mAllGames.push_back(game);
			mLowerNames.push_back(Utils::String::toLower(game->getName()));
		}
	};

	if (mScope)
	{
		addSystem(mScope);
	}
	else
	{
		for (auto sys : SystemData::sSystemVector)
			addSystem(sys);
	}

	LOG(LogInfo) << "GuiSearchPopup: cached " << mAllGames.size() << " games";
}

void GuiSearchPopup::startSearch(const std::string& query)
{
	cancelSearch();

	if (query.empty())
	{
		mResultList.clear();
		for (auto p : mPlaceholders) delete p;
		mPlaceholders.clear();
		addPlaceholder("TYPE TO SEARCH...");
		return;
	}

	mCancelFlag.store(false);
	mResultsReady.store(false);

	std::string lowerQuery = Utils::String::toLower(query);

	mSearchThread = std::thread([this, lowerQuery]() {
		std::vector<FileData*> results;
		for (size_t i = 0; i < mAllGames.size(); i++)
		{
			if (mCancelFlag.load())
				return;
			if (mLowerNames[i].find(lowerQuery) != std::string::npos)
				results.push_back(mAllGames[i]);
		}
		std::sort(results.begin(), results.end(), [](FileData* a, FileData* b) {
			return Utils::String::toLower(a->getName()) < Utils::String::toLower(b->getName());
		});
		{
			std::lock_guard<std::mutex> lock(mResultMutex);
			mPendingResults = std::move(results);
		}
		mResultsReady.store(true);
	});
}

void GuiSearchPopup::cancelSearch()
{
	mCancelFlag.store(true);
	if (mSearchThread.joinable())
		mSearchThread.join();
}

void GuiSearchPopup::populateResultsList(const std::vector<FileData*>& results)
{
	mResultList.clear();
	for (auto p : mPlaceholders) delete p;
	mPlaceholders.clear();
	mCurrentResults.clear();

	if (results.empty())
	{
		addPlaceholder("NO RESULTS FOUND");
		return;
	}

	for (auto game : results)
	{
		std::string displayName = game->getName();
		if (!mScope)
			displayName += " [" + Utils::String::toUpper(game->getSystem()->getName()) + "]";
		mResultList.add(displayName, game, 0);
		mCurrentResults.push_back(game);
	}
}

void GuiSearchPopup::addPlaceholder(const std::string& text)
{
	// We need a valid system for the placeholder FileData
	SystemData* sys = mScope;
	if (!sys && !SystemData::sSystemVector.empty())
		sys = SystemData::sSystemVector.front();
	if (!sys)
		return;

	FileData* placeholder = new FileData(PLACEHOLDER, text,
		sys->getSystemEnvData(), sys);
	mPlaceholders.push_back(placeholder);
	mResultList.add(text, placeholder, 0);
}

void GuiSearchPopup::updateFocusVisuals()
{
	bool charRowFocused = (mFocus == FOCUS_CHAR_ROW);
	mCharRow.setFocused(charRowFocused);

	if (charRowFocused)
	{
		mResultList.setSelectorColor(0x00000000);
		mResultList.setSelectorColorEnd(0x00000000);
	}
	else
	{
		mResultList.setSelectorColor(mResultListSelectorColor);
		mResultList.setSelectorColorEnd(mResultListSelectorColorEnd);
	}

	// Refresh the help bar to reflect the current focus state
	updateHelpPrompts();
}

void GuiSearchPopup::launch(FileData* game)
{
	ViewController::get()->launch(game);
}

void GuiSearchPopup::update(int deltaTime)
{
	if (mResultsReady.load())
	{
		std::vector<FileData*> results;
		{
			std::lock_guard<std::mutex> lock(mResultMutex);
			results = std::move(mPendingResults);
		}
		mResultsReady.store(false);
		populateResultsList(results);
		updateInfoPanel();
	}

	GuiComponent::update(deltaTime);
}

bool GuiSearchPopup::input(InputConfig* config, Input input)
{
	if (input.value != 0)
	{
		// Close on B only
		if (config->isMappedTo("b", input))
		{
			cancelSearch();
			delete this;
			return true;
		}

		// RT: move focus back to char row (no-op if already there)
		if (config->isMappedTo("righttrigger", input))
		{
			if (mFocus == FOCUS_RESULT_LIST)
			{
				mResultList.stopScrolling(true);
				mFocus = FOCUS_CHAR_ROW;
				updateFocusVisuals();
			}
			return true;
		}

		// Main menu — always available
		if (config->isMappedTo("start", input) &&
			!(UIModeController::getInstance()->isUIModeKid() &&
			  Settings::getInstance()->getBool("DisableKidStartMenu")))
		{
			mWindow->pushGui(new GuiMenu(mWindow));
			return true;
		}

		// Favorites + random — only when a real game is selected in the result list
		if (mFocus == FOCUS_RESULT_LIST)
		{
			FileData* selected = (mResultList.size() > 0) ? mResultList.getSelected() : nullptr;
			bool hasGame = selected && selected->getType() == GAME;

			if (config->isMappedTo("y", input) && !UIModeController::getInstance()->isUIModeKid())
			{
				if (hasGame)
					CollectionSystemManager::get()->toggleGameInCollection(selected);
				return true;
			}

			if (config->isMappedTo("x", input))
			{
				// Jump to a random entry in the results
				if (mResultList.size() > 1)
				{
					int idx = std::rand() % mResultList.size();
					mResultList.setCursorIndex(idx);
				}
				return true;
			}

			if (config->isMappedTo("select", input) && !UIModeController::getInstance()->isUIModeKid())
			{
				SystemData* sys = hasGame ? selected->getSystem() : mScope;
				if (!sys && !SystemData::sSystemVector.empty())
					sys = SystemData::sSystemVector.front();
				if (sys)
				{
					auto jumpFiles = mCurrentResults;
					mWindow->pushGui(new GuiGamelistOptions(mWindow, sys, jumpFiles,
						[this](int idx) { mResultList.setCursorIndex(idx); }));
				}
				return true;
			}
		}

		if (mFocus == FOCUS_CHAR_ROW)
		{
			// Physical keyboard special keys
			if (config->getDeviceId() == DEVICE_KEYBOARD)
			{
				if (input.id == SDLK_DOWN)
				{
					mFocus = FOCUS_RESULT_LIST;
					mResultList.setCursorIndex(0);
					updateFocusVisuals();
					return true;
				}
				if (input.id == SDLK_BACKSPACE) { editBackspace();   return true; }
				if (input.id == SDLK_LEFT)      { editCursorLeft();  return true; }
				if (input.id == SDLK_RIGHT)     { editCursorRight(); return true; }
				if (input.id == SDLK_DELETE)
				{
					if (mCursorPos < mQuery.size())
					{
						size_t next = Utils::String::nextCursor(mQuery, mCursorPos);
						mQuery.erase(mCursorPos, next - mCursorPos);
					}
					updateSearchDisplay();
					startSearch(mQuery);
					return true;
				}
				if (input.id == SDLK_HOME)
				{
					mCursorPos = 0;
					updateSearchDisplay();
					return true;
				}
				if (input.id == SDLK_END)
				{
					mCursorPos = mQuery.size();
					updateSearchDisplay();
					return true;
				}
			}
			if (config->isMappedLike("down", input))
			{
				mFocus = FOCUS_RESULT_LIST;
				updateFocusVisuals();
				return true;
			}

			if (mCharRow.input(config, input))
				return true;
		}
		else // FOCUS_RESULT_LIST
		{
			if (config->isMappedLike("up", input))
			{
				if (mResultList.getCursorIndex() == 0)
				{
					mResultList.stopScrolling(true);
					mFocus = FOCUS_CHAR_ROW;
					updateFocusVisuals();
					return true;
				}
				mResultList.input(config, input);
				return true;
			}
			if (config->isMappedLike("down", input))
			{
				if (mResultList.size() == 0 ||
					mResultList.getCursorIndex() == (int)mResultList.size() - 1)
				{
					mResultList.stopScrolling(true);
					mFocus = FOCUS_CHAR_ROW;
					updateFocusVisuals();
					return true;
				}
				mResultList.input(config, input);
				return true;
			}
			if (config->isMappedTo("a", input))
			{
				FileData* cursor = mResultList.size() > 0 ? mResultList.getSelected() : nullptr;
				if (cursor && cursor->getType() == GAME)
					launch(cursor);
				return true;
			}
			if (mResultList.input(config, input))
				return true;
		}
	}
	else
	{
		// Button release — forward to stop scrolling
		if (mFocus == FOCUS_RESULT_LIST)
			mResultList.input(config, input);
		else if (mFocus == FOCUS_CHAR_ROW)
			mCharRow.input(config, input);
	}

	return GuiComponent::input(config, input);
}

void GuiSearchPopup::textInput(const char* text)
{
	// Reject control characters (backspace sends both KEYDOWN and TEXTINPUT)
	if ((unsigned char)text[0] < 0x20)
		return;

	if (mFocus != FOCUS_CHAR_ROW)
		return;

	mQuery.insert(mCursorPos, text);
	mCursorPos += strlen(text);
	updateSearchDisplay();
	startSearch(mQuery);
}

void GuiSearchPopup::render(const Transform4x4f& parentTrans)
{
	Transform4x4f trans = parentTrans * getTransform();

	// Dark background panel
	Renderer::setMatrix(trans);
	Renderer::drawRect(0.0f, 0.0f, mSize.x(), mSize.y(), 0x000000E0, 0x000000E0);

	renderChildren(trans);
}

std::vector<HelpPrompt> GuiSearchPopup::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;
	if (mFocus == FOCUS_CHAR_ROW)
	{
		prompts.push_back(HelpPrompt("left/right", "choose"));
		prompts.push_back(HelpPrompt("a", "type"));
		prompts.push_back(HelpPrompt("up/down", "results"));
	}
	else
	{
		prompts.push_back(HelpPrompt("up/down", "choose"));
		prompts.push_back(HelpPrompt("a", "launch"));
		prompts.push_back(HelpPrompt("x", "random"));
		if (!UIModeController::getInstance()->isUIModeKid())
		{
			prompts.push_back(HelpPrompt("y", "favorite"));
			prompts.push_back(HelpPrompt("select", "options"));
		}
	}
	prompts.push_back(HelpPrompt("b", "close"));
	if (!UIModeController::getInstance()->isUIModeKid())
		prompts.push_back(HelpPrompt("start", "menu"));
	if (mFocus == FOCUS_RESULT_LIST)
		prompts.push_back(HelpPrompt("rt", "keyboard"));
	return prompts;
}

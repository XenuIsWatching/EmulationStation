#include "views/gamelist/SearchGameListView.h"

#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "SystemData.h"
#include "ThemeData.h"
#include "Settings.h"
#include "Log.h"
#include "utils/StringUtil.h"
#include <cstring>

SearchGameListView::SearchGameListView(Window* window, FileData* root)
	: IGameListView(window, root),
	  mBackground(window), mHeaderText(window), mHeaderImage(window),
	  mSearchText(window), mCharRow(window), mResultList(window),
	  mCancelFlag(false), mResultsReady(false),
	  mFocus(FOCUS_CHAR_ROW), mIsActive(false), mCursorPos(0)
{
	const float screenW = mSize.x();
	const float screenH = mSize.y();

	// Background (themed via "background" element)
	mBackground.setResize(screenW, screenH);
	mBackground.setDefaultZIndex(0);
	addChild(&mBackground);

	// Header logo/text (themed via "logo" / "logoText" elements)
	mHeaderText.setText("SEARCH");
	mHeaderText.setSize(screenW, screenH * 0.185f);
	mHeaderText.setPosition(0, 0);
	mHeaderText.setHorizontalAlignment(ALIGN_CENTER);
	mHeaderText.setDefaultZIndex(50);
	addChild(&mHeaderText);

	// mHeaderImage is added/removed in onThemeChanged depending on whether the theme has a logo image
	mHeaderImage.setResize(0, screenH * 0.185f);
	mHeaderImage.setOrigin(0.5f, 0.0f);
	mHeaderImage.setPosition(screenW / 2.0f, 0);
	mHeaderImage.setDefaultZIndex(50);

	// Search text display (default position; overridden in onThemeChanged)
	mSearchText.setText("");
	mSearchText.setSize(screenW, screenH * 0.05f);
	mSearchText.setPosition(0, screenH * 0.20f);
	mSearchText.setHorizontalAlignment(ALIGN_CENTER);
	mSearchText.setColor(0xCCCCCCFF);
	mSearchText.setFont(Font::get(FONT_SIZE_MEDIUM));
	mSearchText.setDefaultZIndex(40);
	addChild(&mSearchText);

	// Character row (default position; overridden in onThemeChanged)
	mCharRow.setSize(screenW, screenH * 0.06f);
	mCharRow.setPosition(0, screenH * 0.26f);
	mCharRow.setDefaultZIndex(40);
	addChild(&mCharRow);

	mCharRow.setCharSelectedCallback([this](const std::string& ch) {
		mQuery.insert(mCursorPos, ch);
		mCursorPos += ch.size();
		updateSearchDisplay();
		startSearch(mQuery);
	});

	mCharRow.setBackspaceCallback([this]() {
		if (mCursorPos > 0)
		{
			size_t prev = Utils::String::prevCursor(mQuery, mCursorPos);
			mQuery.erase(prev, mCursorPos - prev);
			mCursorPos = prev;
		}
		updateSearchDisplay();
		startSearch(mQuery);
	});

	mCharRow.setCursorLeftCallback([this]() {
		if (mCursorPos > 0)
			mCursorPos = Utils::String::prevCursor(mQuery, mCursorPos);
		updateSearchDisplay();
	});

	mCharRow.setCursorRightCallback([this]() {
		if (mCursorPos < mQuery.size())
			mCursorPos = Utils::String::nextCursor(mQuery, mCursorPos);
		updateSearchDisplay();
	});

	// Result list
	float listY = screenH * 0.18f;
	mResultList.setSize(screenW, screenH - listY);
	mResultList.setPosition(0, listY);
	mResultList.setDefaultZIndex(20);
	addChild(&mResultList);

	buildGameCache();
	addPlaceholder("TYPE TO SEARCH...");
}

SearchGameListView::~SearchGameListView()
{
	cancelSearch();

	for (auto extra : mThemeExtras)
	{
		removeChild(extra);
		delete extra;
	}
}

void SearchGameListView::updateSearchDisplay()
{
	// Render cursor as underscore inserted at cursor position
	std::string display = mQuery.substr(0, mCursorPos) + "_" + mQuery.substr(mCursorPos);
	mSearchText.setText(display);
}

void SearchGameListView::buildGameCache()
{
	mAllGames.clear();
	mLowerNames.clear();

	for (auto sys : SystemData::sSystemVector)
	{
		if (!sys->isGameSystem() || sys->isCollection())
			continue;

		std::vector<FileData*> games = sys->getRootFolder()->getFilesRecursive(GAME);
		for (auto game : games)
		{
			mAllGames.push_back(game);
			mLowerNames.push_back(Utils::String::toLower(game->getName()));
		}
	}

	LOG(LogInfo) << "SearchGameListView: cached " << mAllGames.size() << " games for search";
}

void SearchGameListView::startSearch(const std::string& query)
{
	cancelSearch();

	if (query.empty())
	{
		mResultList.clear();
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

		{
			std::lock_guard<std::mutex> lock(mResultMutex);
			mPendingResults = std::move(results);
		}
		mResultsReady.store(true);
	});
}

void SearchGameListView::cancelSearch()
{
	mCancelFlag.store(true);
	if (mSearchThread.joinable())
		mSearchThread.join();
}

void SearchGameListView::populateResultsList(const std::vector<FileData*>& results)
{
	mResultList.clear();

	if (results.empty())
	{
		addPlaceholder("NO RESULTS FOUND");
		return;
	}

	for (auto game : results)
	{
		std::string displayName = game->getName() + " (" + Utils::String::toUpper(game->getSystem()->getName()) + ")";
		mResultList.add(displayName, game, 0);
	}
}

void SearchGameListView::addPlaceholder(const std::string& text)
{
	FileData* placeholder = new FileData(PLACEHOLDER, text, mRoot->getSystem()->getSystemEnvData(), mRoot->getSystem());
	mResultList.add(text, placeholder, 0);
}

void SearchGameListView::update(int deltaTime)
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
	}

	GuiComponent::update(deltaTime);
}

bool SearchGameListView::input(InputConfig* config, Input input)
{
	if (input.value != 0)
	{
		if (config->isMappedTo("b", input))
		{
			onFocusLost();
			ViewController::get()->goToSystemView(mRoot->getSystem());
			return true;
		}

		// Physical keyboard text editing keys
		if (config->getDeviceId() == DEVICE_KEYBOARD)
		{
			if (input.id == SDLK_BACKSPACE)
			{
				// Delete character before cursor
				if (mCursorPos > 0)
				{
					size_t prev = Utils::String::prevCursor(mQuery, mCursorPos);
					mQuery.erase(prev, mCursorPos - prev);
					mCursorPos = prev;
				}
				updateSearchDisplay();
				startSearch(mQuery);
				return true;
			}
			if (input.id == SDLK_DELETE)
			{
				// Delete character at cursor
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
			if (input.id == SDLK_LEFT)
			{
				if (mCursorPos > 0)
					mCursorPos = Utils::String::prevCursor(mQuery, mCursorPos);
				updateSearchDisplay();
				return true;
			}
			if (input.id == SDLK_RIGHT)
			{
				if (mCursorPos < mQuery.size())
					mCursorPos = Utils::String::nextCursor(mQuery, mCursorPos);
				updateSearchDisplay();
				return true;
			}
		}

		if (mFocus == FOCUS_CHAR_ROW)
		{
			if (config->isMappedLike("down", input))
			{
				if (mResultList.size() > 0)
				{
					mFocus = FOCUS_RESULT_LIST;
					return true;
				}
			}

			// Let char row handle left/right/a
			if (mCharRow.input(config, input))
				return true;
		}
		else if (mFocus == FOCUS_RESULT_LIST)
		{
			if (config->isMappedLike("up", input))
			{
				// If at top of list, move focus back to char row
				FileData* cursor = getCursor();
				if (cursor && mResultList.size() > 0)
				{
					// Check if we're at the first entry
					int viewportTop = mResultList.getViewportTop();
					// Simple check: if pressing up and cursor is at position 0
					FileData* first = nullptr;
					if (mResultList.size() > 0)
					{
						// Get the selected entry - if it's the first one, go to char row
						// We use a slightly different approach: try input, if cursor doesn't change, we're at top
						int prevCursor = getViewportTop();
						bool consumed = mResultList.input(config, input);
						if (getCursor() == cursor)
						{
							// Cursor didn't move - we're at the top
							mFocus = FOCUS_CHAR_ROW;
							return true;
						}
						return consumed;
					}
				}
				mFocus = FOCUS_CHAR_ROW;
				return true;
			}

			if (config->isMappedTo("a", input))
			{
				FileData* cursor = getCursor();
				if (cursor && cursor->getType() == GAME)
				{
					launch(cursor);
					return true;
				}
				return true;
			}

			// Let result list handle other input (up/down scrolling)
			if (mResultList.input(config, input))
				return true;
		}
	}
	else
	{
		// Button release - forward to result list if focused to stop scrolling
		if (mFocus == FOCUS_RESULT_LIST)
			mResultList.input(config, input);
	}

	return IGameListView::input(config, input);
}

void SearchGameListView::textInput(const char* text)
{
	if (!mIsActive || text[0] == '\0')
		return;

	mQuery.insert(mCursorPos, text);
	mCursorPos += strlen(text);
	updateSearchDisplay();
	startSearch(mQuery);
}

void SearchGameListView::onShow()
{
	mIsActive = true;
	GuiComponent::onShow();
}

void SearchGameListView::onHide()
{
	mIsActive = false;
	GuiComponent::onHide();
}

void SearchGameListView::render(const Transform4x4f& parentTrans)
{
	Transform4x4f trans = parentTrans * getTransform();

	float scaleX = trans.r0().x();
	float scaleY = trans.r1().y();

	Vector2i pos((int)Math::round(trans.translation()[0]), (int)Math::round(trans.translation()[1]));
	Vector2i size((int)Math::round(mSize.x() * scaleX), (int)Math::round(mSize.y() * scaleY));

	Renderer::pushClipRect(pos, size);
	renderChildren(trans);
	Renderer::popClipRect();
}

FileData* SearchGameListView::getCursor()
{
	if (mResultList.size() == 0)
		return nullptr;
	return mResultList.getSelected();
}

void SearchGameListView::setCursor(FileData* file, bool /*refreshListCursorPos*/)
{
	if (file)
		mResultList.setCursor(file);
}

int SearchGameListView::getViewportTop()
{
	return mResultList.getViewportTop();
}

void SearchGameListView::setViewportTop(int index)
{
	mResultList.setViewportTop(index);
}

void SearchGameListView::onFileChanged(FileData* /*file*/, FileChangeType /*change*/)
{
	// Rebuild cache when files change
	buildGameCache();
	if (!mQuery.empty())
		startSearch(mQuery);
}

void SearchGameListView::onThemeChanged(const std::shared_ptr<ThemeData>& theme)
{
	using namespace ThemeFlags;

	mBackground.applyTheme(theme, getName(), "background", ALL);
	mHeaderImage.applyTheme(theme, getName(), "logo", ALL);
	mHeaderText.applyTheme(theme, getName(), "logoText", ALL);

	// Swap header image / text based on whether the theme provides a logo image
	if (mHeaderImage.hasImage())
	{
		removeChild(&mHeaderText);
		addChild(&mHeaderImage);
	}
	else
	{
		addChild(&mHeaderText);
		removeChild(&mHeaderImage);
	}

	// Apply theme to the result list
	mResultList.applyTheme(theme, getName(), "gamelist", ALL);

	// Position search-specific elements (search text + char row) just above the result list
	float listY = mResultList.getPosition().y();
	float rowH  = mSize.y() * 0.06f;
	mCharRow.setPosition(0, listY - rowH);
	mCharRow.setSize(mSize.x(), rowH);
	mSearchText.setPosition(0, listY - rowH * 2.0f);
	mSearchText.setSize(mSize.x(), rowH);

	sortChildren();
}

void SearchGameListView::launch(FileData* game)
{
	ViewController::get()->launch(game);
}

void SearchGameListView::remove(FileData* /*game*/, bool /*deleteFile*/, bool /*refreshView*/)
{
	// Not applicable for search results
}

void SearchGameListView::onFocusLost()
{
	mResultList.stopScrolling(true);
}

std::vector<HelpPrompt> SearchGameListView::getHelpPrompts()
{
	std::vector<HelpPrompt> prompts;

	if (mFocus == FOCUS_CHAR_ROW)
	{
		prompts.push_back(HelpPrompt("left/right", "choose"));
		prompts.push_back(HelpPrompt("a", "select"));
		prompts.push_back(HelpPrompt("down", "results"));
	}
	else
	{
		prompts.push_back(HelpPrompt("up/down", "choose"));
		prompts.push_back(HelpPrompt("a", "launch"));
		prompts.push_back(HelpPrompt("up", "keyboard"));
	}
	prompts.push_back(HelpPrompt("b", "back"));

	return prompts;
}

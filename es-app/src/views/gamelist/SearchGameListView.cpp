#include "views/gamelist/SearchGameListView.h"

#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "SystemData.h"
#include "Settings.h"
#include "Log.h"
#include "utils/StringUtil.h"

SearchGameListView::SearchGameListView(Window* window, FileData* root)
	: IGameListView(window, root),
	  mHeaderText(window), mSearchText(window),
	  mCharRow(window), mResultList(window),
	  mCancelFlag(false), mResultsReady(false),
	  mFocus(FOCUS_CHAR_ROW)
{
	const float screenW = mSize.x();
	const float screenH = mSize.y();

	// Header "Search"
	mHeaderText.setText("SEARCH");
	mHeaderText.setSize(screenW, screenH * 0.06f);
	mHeaderText.setPosition(0, 0);
	mHeaderText.setHorizontalAlignment(ALIGN_CENTER);
	mHeaderText.setColor(0xFFFFFFFF);
	mHeaderText.setFont(Font::get(FONT_SIZE_LARGE));
	mHeaderText.setDefaultZIndex(50);
	addChild(&mHeaderText);

	// Search text display
	mSearchText.setText("");
	mSearchText.setSize(screenW, screenH * 0.05f);
	mSearchText.setPosition(0, screenH * 0.06f);
	mSearchText.setHorizontalAlignment(ALIGN_CENTER);
	mSearchText.setColor(0xCCCCCCFF);
	mSearchText.setFont(Font::get(FONT_SIZE_MEDIUM));
	mSearchText.setDefaultZIndex(40);
	addChild(&mSearchText);

	// Character row
	mCharRow.setSize(screenW, screenH * 0.06f);
	mCharRow.setPosition(0, screenH * 0.11f);
	mCharRow.setDefaultZIndex(40);
	addChild(&mCharRow);

	mCharRow.setCharSelectedCallback([this](const std::string& ch) {
		mQuery += ch;
		mSearchText.setText(mQuery + "_");
		startSearch(mQuery);
	});

	mCharRow.setBackspaceCallback([this]() {
		if (!mQuery.empty())
		{
			size_t cursor = Utils::String::prevCursor(mQuery, mQuery.size());
			mQuery.erase(cursor);
		}
		if (mQuery.empty())
			mSearchText.setText("");
		else
			mSearchText.setText(mQuery + "_");
		startSearch(mQuery);
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
	if (text[0] == '\0')
		return;

	mQuery += text;
	mSearchText.setText(mQuery + "_");
	startSearch(mQuery);
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

void SearchGameListView::onThemeChanged(const std::shared_ptr<ThemeData>& /*theme*/)
{
	// Search view doesn't heavily rely on theming for now
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

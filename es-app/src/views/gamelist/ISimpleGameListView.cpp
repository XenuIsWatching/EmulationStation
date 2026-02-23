#include "views/gamelist/ISimpleGameListView.h"

#include "guis/GuiSettings.h"
#include "components/ImageComponent.h"
#include "components/TextComponent.h"
#include "views/UIModeController.h"
#include "views/ViewController.h"
#include "CollectionSystemManager.h"
#include "Scripting.h"
#include "Settings.h"
#include "Sound.h"
#include "SystemData.h"
#include "Window.h"

namespace
{
	class RomPromptTextComponent : public TextComponent
	{
	public:
		RomPromptTextComponent(Window* window, const std::string& text, const std::shared_ptr<Font>& font, unsigned int color)
			: TextComponent(window, text, font, color)
		{
		}

		std::vector<HelpPrompt> getHelpPrompts() override
		{
			std::vector<HelpPrompt> prompts;
			prompts.push_back(HelpPrompt("a", "launch"));
			prompts.push_back(HelpPrompt("y", "preferred"));
			return prompts;
		}
	};

	class RomSelectionMenu : public GuiSettings
	{
	public:
		RomSelectionMenu(Window* window, FileData* game)
			: GuiSettings(window, "ROMS"), mGame(game), mPreferredChanged(false)
		{
			buildRows();
		}

		~RomSelectionMenu()
		{
			if(!mPreferredChanged || mGame == nullptr)
				return;

			// Preferred ROM changes alter effective media/launch selection, so notify both
			// the visible entry and canonical source entry to refresh info panels immediately.
			ViewController::get()->onFileChanged(mGame, FILE_METADATA_CHANGED);
			FileData* source = mGame->getSourceFileData();
			if(source != mGame)
				ViewController::get()->onFileChanged(source, FILE_METADATA_CHANGED);
		}

	private:
		struct RomRowWidgets
		{
			std::shared_ptr<ImageComponent> star;
			std::shared_ptr<TextComponent> name;
		};

		void buildRows()
		{
			if(mGame == nullptr)
				return;

			const std::vector<RomData>& roms = mGame->getRoms();
			for(unsigned int i = 0; i < roms.size(); ++i)
			{
				ComponentListRow row;

				auto star = std::make_shared<ImageComponent>(mWindow);
				const float starSize = Font::get(FONT_SIZE_MEDIUM)->getLetterHeight();
				star->setImage(":/star_filled.svg");
				star->setResize(starSize, starSize);
				star->setVisible(roms[i].preferred);

				std::string romName = roms[i].romName.empty() ? mGame->getName() : roms[i].romName;
				auto label = std::make_shared<RomPromptTextComponent>(mWindow, romName, Font::get(FONT_SIZE_MEDIUM), 0x777777FF);

				row.addElement(star, false, false);
				row.addElement(label, true);
				row.input_handler = [this, i](InputConfig* config, Input input) -> bool {
					if(input.value == 0)
						return false;

					if(config->isMappedTo("a", input))
					{
						launchRom(i);
						return true;
					}

					if(config->isMappedTo("y", input))
					{
						setPreferredRom(i);
						return true;
					}

					return false;
				};

				addRow(row);

				RomRowWidgets widgets;
				widgets.star = star;
				widgets.name = label;
				mRows.push_back(widgets);
			}

			updatePreferredIndicator();
		}

		void setPreferredRom(unsigned int index)
		{
			if(mGame == nullptr)
				return;

			FileData* source = mGame->getSourceFileData();
			std::vector<RomData>& roms = source->getRomsMutable();
			if(index >= roms.size())
				return;

			for(unsigned int i = 0; i < roms.size(); ++i)
				roms[i].preferred = (i == index);

			// Mark metadata dirty so onMetaDataSavePoint() persists updated ROM preference.
			source->metadata.set("name", source->metadata.get("name"));
			source->getSystem()->onMetaDataSavePoint();

			if(mGame != source)
				mGame->refreshMetadata();

			mPreferredChanged = true;
			updatePreferredIndicator();
		}

		void launchRom(unsigned int index)
		{
			if(mGame == nullptr)
				return;

			FileData* selectedEntry = mGame;
			FileData* source = selectedEntry->getSourceFileData();
			const std::vector<RomData>& roms = source->getRoms();
			if(index >= roms.size() || roms[index].path.empty())
				return;

			const std::string launchPath = roms[index].path;
			// Keep this popup alive so returning from gameplay lands back on the
			// same ROM selection screen and highlighted row.
			source->launchGame(mWindow, launchPath);
			ViewController::get()->onFileChanged(source, FILE_METADATA_CHANGED);
			if(selectedEntry != source)
				ViewController::get()->onFileChanged(selectedEntry, FILE_METADATA_CHANGED);
		}

		void updatePreferredIndicator()
		{
			if(mGame == nullptr)
				return;

			const std::vector<RomData>& roms = mGame->getRoms();
			for(unsigned int i = 0; i < roms.size() && i < mRows.size(); ++i)
				mRows[i].star->setVisible(roms[i].preferred);
		}

		FileData* mGame;
		bool mPreferredChanged;
		std::vector<RomRowWidgets> mRows;
	};

	void openRomSelectionMenu(Window* window, FileData* game)
	{
		if(game == nullptr || game->getType() != GAME)
			return;

		const std::vector<RomData>& roms = game->getRoms();
		if(roms.empty())
			return;

		window->pushGui(new RomSelectionMenu(window, game));
	}
}

ISimpleGameListView::ISimpleGameListView(Window* window, FileData* root) : IGameListView(window, root),
	mHeaderText(window), mHeaderImage(window), mBackground(window)
{
	mHeaderText.setText("Logo Text");
	mHeaderText.setSize(mSize.x(), 0);
	mHeaderText.setPosition(0, 0);
	mHeaderText.setHorizontalAlignment(ALIGN_CENTER);
	mHeaderText.setDefaultZIndex(50);

	mHeaderImage.setResize(0, mSize.y() * 0.185f);
	mHeaderImage.setOrigin(0.5f, 0.0f);
	mHeaderImage.setPosition(mSize.x() / 2, 0);
	mHeaderImage.setDefaultZIndex(50);

	mBackground.setResize(mSize.x(), mSize.y());
	mBackground.setDefaultZIndex(0);

	addChild(&mHeaderText);
	addChild(&mBackground);
}

void ISimpleGameListView::onThemeChanged(const std::shared_ptr<ThemeData>& theme)
{
	using namespace ThemeFlags;
	mBackground.applyTheme(theme, getName(), "background", ALL);
	mHeaderImage.applyTheme(theme, getName(), "logo", ALL);
	mHeaderText.applyTheme(theme, getName(), "logoText", ALL);

	// Remove old theme extras
	for (auto extra : mThemeExtras)
	{
		removeChild(extra);
		delete extra;
	}
	mThemeExtras.clear();

	// Add new theme extras
	mThemeExtras = ThemeData::makeExtras(theme, getName(), mWindow);
	for (auto extra : mThemeExtras)
	{
		addChild(extra);
	}

	if(mHeaderImage.hasImage())
	{
		removeChild(&mHeaderText);
		addChild(&mHeaderImage);
	}else{
		addChild(&mHeaderText);
		removeChild(&mHeaderImage);
	}
}

void ISimpleGameListView::onFileChanged(FileData* /*file*/, FileChangeType change)
{
	if(change == FILE_METADATA_CHANGED)
	{
		FileData* cursor = getCursor();
		// Keep metadata updates lightweight (avoid full list repopulate/flicker) while
		// still forcing current row/panel refresh for preferred-ROM media changes.
		if(cursor != nullptr && !cursor->isPlaceHolder())
			setCursor(cursor, true);
		return;
	}

	// we could be tricky here to be efficient;
	// but this shouldn't happen very often so we'll just always repopulate
	FileData* cursor = getCursor();
	if (!cursor->isPlaceHolder()) {
		populateList(cursor->getParent()->getChildrenListToDisplay());
		setCursor(cursor);
	}
	else
	{
		populateList(mRoot->getChildrenListToDisplay());
		setCursor(cursor);
	}
}

bool ISimpleGameListView::input(InputConfig* config, Input input)
{
	if(input.value != 0)
	{
		if(config->isMappedTo("a", input))
		{
			FileData* cursor = getCursor();
			if(cursor->getType() == GAME)
			{
				Sound::getFromTheme(getTheme(), getName(), "launch")->play();
				launch(cursor);
			}else{
				// it's a folder
				if(cursor->getChildren().size() > 0)
				{
					mCursorStack.push(cursor);
					populateList(cursor->getChildrenListToDisplay());
					FileData* cursor = getCursor();
					setCursor(cursor);
				}
			}

			return true;
		}else if(config->isMappedTo("b", input))
		{
			if(mCursorStack.size())
			{
				populateList(mCursorStack.top()->getParent()->getChildrenListToDisplay());
				setCursor(mCursorStack.top());
				mCursorStack.pop();
				Sound::getFromTheme(getTheme(), getName(), "back")->play();
			}else{
				onFocusLost();
				SystemData* systemToView = getCursor()->getSystem();
				if (systemToView->isCollection())
				{
					systemToView = CollectionSystemManager::get()->getSystemToView(systemToView);
				}
				ViewController::get()->goToSystemView(systemToView);
			}

			return true;
		}else if(config->isMappedLike(getQuickSystemSelectRightButton(), input))
		{
			if(Settings::getInstance()->getBool("QuickSystemSelect"))
			{
				onFocusLost();
				ViewController::get()->goToNextGameList();
				return true;
			}
		}else if(config->isMappedLike(getQuickSystemSelectLeftButton(), input))
		{
			if(Settings::getInstance()->getBool("QuickSystemSelect"))
			{
				onFocusLost();
				ViewController::get()->goToPrevGameList();
				return true;
			}
		}else if (config->isMappedTo("x", input))
		{
			if (mRoot->getSystem()->isGameSystem())
			{
				FileData* cursor = getCursor();
				if(cursor->getType() == GAME)
				{
					openRomSelectionMenu(mWindow, cursor);
					return true;
				}
			}
		}else if (config->isMappedTo("y", input) && !UIModeController::getInstance()->isUIModeKid())
		{
			if(mRoot->getSystem()->isGameSystem())
			{
				if (CollectionSystemManager::get()->toggleGameInCollection(getCursor()))
				{
					return true;
				}
			}
		}
	}

	FileData* cursor = getCursor();
	SystemData* system = this->mRoot->getSystem();
    	if (system != NULL) {
            Scripting::fireEvent("game-select", system->getName(), cursor->getPath(), cursor->getName(), "input");
        }
	else
	{
	    Scripting::fireEvent("game-select", "NULL", "NULL", "NULL", "input");
	}
	return IGameListView::input(config, input);
}
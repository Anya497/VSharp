import torch
from agent.connection_manager import ConnectionManager
from agent.n_agent import get_validation_maps
from constants import Constant
from logger.setup import setup_loggers
from ml.model_wrappers.torch_model import TorchModelWrapper
from ml.models import GCN
from ml.mutation_gen import MutationProportions, Mutator, MutatorConfig
from r_learn import r_learn


def main():
    setup_loggers()
    socket_urls = [Constant.DEFAULT_GAMESERVER_URL]
    cm = ConnectionManager(socket_urls)

    model = GCN(hidden_channels=64)
    optimizer = torch.optim.Adam(model.parameters(), lr=0.01)
    criterion = torch.nn.CrossEntropyLoss()

    epochs = 2
    max_steps = 2
    n_models = 10

    models = [TorchModelWrapper(model, optimizer, criterion) for _ in range(n_models)]

    maps = get_validation_maps(cm)

    mutator_config = MutatorConfig(
        proportions=MutationProportions(
            n_tops=4,
            averaged_n_tops=1,
            n_averaged_all=1,
            random_n_tops_averaged_mutations=2,
            random_all_averaged_mutations=2,
        ),
        mutation_volume=0.2,
        mutation_freq=0.2,
    )

    r_learn(
        epochs, max_steps, models, maps, Mutator(mutator_config, TorchModelWrapper), cm
    )

    cm.close()


if __name__ == "__main__":
    main()
